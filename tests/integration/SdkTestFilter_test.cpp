/**
 * @file SdkTestFilter_test.cpp
 * @brief This file defines some tests for the sorting of the results from the search command
 */

#include "gmock/gmock.h"
#include "megaapi.h"
#include "SdkTest_test.h"

#include <optional>

namespace
{
/**
 * @class NodeCommonInfo
 * @brief Common information shared both by files and directories
 */
struct NodeCommonInfo
{
    std::string name;
    std::optional<unsigned int> label = std::nullopt; // e.g. MegaNode::NODE_LBL_PURPLE
    bool fav = false;
};

struct FileNodeInfo: public NodeCommonInfo
{
    unsigned int size = 0;
    int64_t mtime = ::mega::MegaApi::INVALID_CUSTOM_MOD_TIME;

    FileNodeInfo(const std::string& _name,
                 const std::optional<unsigned int>& _label = std::nullopt,
                 const bool _fav = false,
                 const unsigned int _size = 0,
                 const std::chrono::seconds secondsSinceMod = 0s):
        NodeCommonInfo{_name, _label, _fav},
        size(_size)
    {
        static const auto refTime = m_time();
        if (auto nSecs = secondsSinceMod.count(); nSecs != 0)
        {
            mtime = refTime - nSecs;
        }
    }
};

struct DirNodeInfo;

typedef std::variant<FileNodeInfo, DirNodeInfo> NodeInfo;

struct DirNodeInfo: public NodeCommonInfo
{
    std::vector<NodeInfo> childs{};

    DirNodeInfo(const std::string& _name,
                const std::vector<NodeInfo>& _childs = {},
                const std::optional<unsigned int>& _label = std::nullopt,
                const bool _fav = false):
        NodeCommonInfo{_name, _label, _fav},
        childs(_childs)
    {}
};

// Needed forward declaration for recursive call
void processDirChildName(const DirNodeInfo&, std::vector<std::string>&);

/**
 * @brief Put the name of the node and their children (if any) in the given vector
 */
void processNodeName(const NodeInfo& node, std::vector<std::string>& names)
{
    std::visit(
        [&names](const auto& arg)
        {
            names.push_back(arg.name);
            if constexpr (std::is_same_v<decltype(arg), const DirNodeInfo&>)
            {
                processDirChildName(arg, names);
            }
        },
        node);
}

/**
 * @brief Aux function to call inside the visitor. Puts the names of the children in names.
 */
void processDirChildName(const DirNodeInfo& dir, std::vector<std::string>& names)
{
    for (const auto& child: dir.childs)
    {
        processNodeName(child, names);
    }
}

/**
 * @brief Wrapper around processNodeName. Returns the names in the tree specified by node
 *
 * @note The tree is iterated using a depth-first approach
 */
std::vector<std::string> getNodeNames(const NodeInfo& node)
{
    std::vector<std::string> result;
    processNodeName(node, result);
    return result;
}

/**
 * @class LocalTempFile
 * @brief Helper class to apply RAII when creating a file locally
 *
 */
struct LocalTempFile
{
    LocalTempFile(const fs::path& _filePath, const unsigned int fileSizeBytes):
        filePath(_filePath)
    {
        std::ofstream outFile(filePath, std::ios::binary);
        if (!outFile.is_open())
        {
            throw std::runtime_error("Can't open the file: " + _filePath.string());
        }
        std::vector<char> buffer(fileSizeBytes, 0);
        outFile.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    }

    ~LocalTempFile()
    {
        if (fs::exists(filePath))
        {
            fs::remove(filePath);
        }
    }

private:
    fs::path filePath;
};

/**
 * @brief Aux function to get a vector with the names of the nodes in a given MegaNodeList
 */
std::vector<std::string> toNamesVector(const MegaNodeList& nodes)
{
    std::vector<std::string> result;
    result.reserve(static_cast<size_t>(nodes.size()));
    for (int i = 0; i < nodes.size(); ++i)
    {
        result.push_back(nodes.get(i)->getName());
    }
    return result;
}
}

/**
 * @class SdkTestFilter
 * @brief Fixture that sets up a scenario to perform the search.
 *
 * The main idea is to create a directory tree inside the ROOT_TEST_NODE_DIR to perform the search
 * inside it. The tree is specified by the ELEMENTS member, where all the special attributes for
 * files/directories are also defined.
 *
 */
class SdkTestFilter: public SdkTest
{
private:
    unique_ptr<MegaNode> rootTestDirNode;

public:
    static constexpr std::string_view ROOT_TEST_NODE_DIR = "SDK_TEST_FILTER_AUX_DIR";

    const std::vector<NodeInfo> ELEMENTS{
        FileNodeInfo("testFile1", MegaNode::NODE_LBL_RED),
        DirNodeInfo("Dir1",
                    {FileNodeInfo("testFile2", MegaNode::NODE_LBL_ORANGE, true, 15, 100s),
                     FileNodeInfo("testFile3", MegaNode::NODE_LBL_YELLOW, false, 35, 500s),
                     DirNodeInfo("Dir11",
                                 {
                                     FileNodeInfo("testFile4"),
                                 })},
                    MegaNode::NODE_LBL_PURPLE,
                    true),
        DirNodeInfo("Dir2", {FileNodeInfo("testFile5", MegaNode::NODE_LBL_BLUE, true, 20, 200s)}),
        FileNodeInfo("testFile6", {}, true, 10, 300s),
    };

    void SetUp() override
    {
        SdkTest::SetUp();
        getAccountsForTest(1);
        createRootTestDir();
        createNodes(ELEMENTS, rootTestDirNode.get());
    }

    /**
     * @brief Get a vector with all the names of the nodes created inside the ROOT_TEST_NODE_DIR
     */
    std::vector<std::string> getAllNodesNames() const
    {
        std::vector<std::string> result;
        std::for_each(ELEMENTS.begin(),
                      ELEMENTS.end(),
                      [&result](const auto& el)
                      {
                          const auto partial = getNodeNames(el);
                          result.insert(result.end(), partial.begin(), partial.end());
                      });
        return result;
    }

    /**
     * @brief Get a filter to use in the search. It is adapted to search from the ROOT_TEST_NODE_DIR
     */
    std::unique_ptr<MegaSearchFilter> getDefaultfilter() const
    {
        std::unique_ptr<MegaSearchFilter> filteringInfo(MegaSearchFilter::createInstance());
        filteringInfo->byLocationHandle(rootTestDirNode->getHandle());
        return filteringInfo;
    }

protected:
    /**
     * @brief Create the ROOT_TEST_NODE_DIR and sets store it internally
     */
    void createRootTestDir()
    {
        const unique_ptr<MegaNode> rootnode(megaApi[0]->getRootNode());
        rootTestDirNode = createRemoteDir(std::string(ROOT_TEST_NODE_DIR), rootnode.get());
    }

    /**
     * @brief Creates the file tree given by the vector of NodeInfo starting from the rootnode
     *
     * @param elements NodeInfo vector to create
     * @param rootnode A pointer to the root node
     */
    void createNodes(const std::vector<NodeInfo>& elements, MegaNode* rootnode)
    {
        for (const auto& element: elements)
        {
            this_thread::sleep_for(1s); // Make sure creation time is different
            std::visit(
                [this, rootnode](const auto& nodeInfo)
                {
                    createNode(nodeInfo, rootnode);
                },
                element);
        }
    }

    /**
     * @brief Creates a file node as a child of the rootnode using the input info.
     */
    void createNode(const FileNodeInfo& fileInfo, MegaNode* rootnode)
    {
        bool check = false;
        mApi[0].mOnNodesUpdateCompletion =
            createOnNodesUpdateLambda(INVALID_HANDLE, MegaNode::CHANGE_TYPE_NEW, check);
        LocalTempFile localFile(fileInfo.name, fileInfo.size);
        MegaHandle file1Handle = 0;
        ASSERT_EQ(MegaError::API_OK,
                  doStartUpload(0,
                                &file1Handle,
                                fileInfo.name.c_str(),
                                rootnode,
                                nullptr /*fileName*/,
                                fileInfo.mtime,
                                nullptr /*appData*/,
                                false /*isSourceTemporary*/,
                                false /*startFirst*/,
                                nullptr /*cancelToken*/))
            << "Cannot upload a test file";

        waitForResponse(&check);
        // important to reset
        resetOnNodeUpdateCompletionCBs();
        unique_ptr<MegaNode> nodeFile(megaApi[0]->getNodeByHandle(file1Handle));
        ASSERT_NE(nodeFile, nullptr)
            << "Cannot get the node for the updated file (error: " << mApi[0].lastError << ")";
        setNodeAdditionalAttributes(fileInfo, nodeFile);
    }

    /**
     * @brief Creates a directory node as a child of the rootnode using the input info.
     */
    void createNode(const DirNodeInfo& dirInfo, MegaNode* rootnode)
    {
        auto dirNode = createRemoteDir(dirInfo.name, rootnode);
        ASSERT_TRUE(dirNode) << "Unable to create directory node with name: " << dirInfo.name;
        setNodeAdditionalAttributes(dirInfo, dirNode);
        createNodes(dirInfo.childs, dirNode.get());
    }

    /**
     * @brief Aux method to create a directory node with the given name inside the given rootnode
     */
    std::unique_ptr<MegaNode> createRemoteDir(const std::string& dirName, MegaNode* rootnode)
    {
        bool check = false;
        mApi[0].mOnNodesUpdateCompletion =
            createOnNodesUpdateLambda(INVALID_HANDLE, MegaNode::CHANGE_TYPE_NEW, check);
        auto folderHandle = createFolder(0, dirName.c_str(), rootnode);
        if (folderHandle == UNDEF)
        {
            return {};
        }
        waitForResponse(&check);
        unique_ptr<MegaNode> dirNode(megaApi[0]->getNodeByHandle(folderHandle));
        resetOnNodeUpdateCompletionCBs();
        return dirNode;
    }

    /**
     * @brief Sets special info such as fav or label for a given node
     */
    void setNodeAdditionalAttributes(const NodeCommonInfo& nodeInfo,
                                     const std::unique_ptr<MegaNode>& node)
    {
        if (!node)
        {
            return;
        }

        ASSERT_EQ(API_OK, synchronousSetNodeFavourite(0, node.get(), nodeInfo.fav))
            << "Error setting fav";

        if (nodeInfo.label)
        {
            ASSERT_EQ(API_OK, synchronousSetNodeLabel(0, node.get(), *nodeInfo.label))
                << "Error setting label";
        }
        else
        {
            ASSERT_EQ(API_OK, synchronousResetNodeLabel(0, node.get())) << "Error resetting label";
        }
    }
};

/**
 * @brief Helper parameterized matcher that checks if the test container contains all the elements
 * in the same order.
 *
 * Example:
 *     std::vector a {1, 5, 7, 8};
 *     ASSERT_THAT(a, ContainsInOrder(std::vector {1, 7, 8}));
 *     ASSERT_THAT(a, testing::Not(ContainsInOrder(std::vector {1, 7, 5})));
 */
MATCHER_P(ContainsInOrder, elements, "")
{
    auto currentEntry = arg.begin();
    for (const auto& exp: elements)
    {
        while (currentEntry != arg.end() && *currentEntry != exp)
        {
            ++currentEntry;
        }
        if (currentEntry == arg.end())
        {
            return false;
        }
    }
    return true;
}

/**
 * @brief SdkTestFilter.SdkGetNodesInOrder
 *
 * Tests all the sorting options available for the MegaApi.search method.
 */
TEST_F(SdkTestFilter, SdkGetNodesInOrder)
{
    using testing::AllOf;
    using testing::NotNull;
    using testing::UnorderedElementsAreArray;

    // Load the default filter to search from ROOT_TEST_NODE_DIR
    std::unique_ptr<MegaSearchFilter> filteringInfo(getDefaultfilter());

    // Default (ORDER_NONE -> Undefined)
    std::unique_ptr<MegaNodeList> searchResults(megaApi[0]->search(filteringInfo.get()));
    ASSERT_THAT(searchResults, NotNull()) << "serach() returned a nullptr";
    EXPECT_THAT(toNamesVector(*searchResults), UnorderedElementsAreArray(getAllNodesNames()))
        << "Unexpected sorting for ORDER_NONE";

    // Alphabetical, dirs first
    std::vector<std::string_view> expected{"Dir1", "Dir11", "Dir2", "testFile1", "testFile6"};
    searchResults.reset(megaApi[0]->search(filteringInfo.get(), MegaApi::ORDER_DEFAULT_ASC));
    ASSERT_THAT(searchResults, NotNull()) << "serach() returned a nullptr";
    EXPECT_THAT(toNamesVector(*searchResults), ContainsInOrder(expected))
        << "Unexpected sorting for ORDER_DEFAULT_ASC";

    // Alphabetical inverted, dirs first
    expected = {"Dir2", "Dir11", "Dir1", "testFile6", "testFile1"};
    searchResults.reset(megaApi[0]->search(filteringInfo.get(), MegaApi::ORDER_DEFAULT_DESC));
    ASSERT_THAT(searchResults, NotNull()) << "serach() returned a nullptr";
    EXPECT_THAT(toNamesVector(*searchResults), ContainsInOrder(expected))
        << "Unexpected sorting for ORDER_DEFAULT_DESC";

    // By size
    expected = {
        "testFile1", // 0
        "testFile6", // 10
        "testFile2", // 15
        "testFile5", // 20
        "testFile3" // 35
    };
    searchResults.reset(megaApi[0]->search(filteringInfo.get(), MegaApi::ORDER_SIZE_ASC));
    ASSERT_THAT(searchResults, NotNull()) << "serach() returned a nullptr";
    EXPECT_THAT(toNamesVector(*searchResults), ContainsInOrder(expected))
        << "Unexpected sorting for ORDER_SIZE_ASC";

    // By size inverted
    std::reverse(expected.begin(), expected.end());
    searchResults.reset(megaApi[0]->search(filteringInfo.get(), MegaApi::ORDER_SIZE_DESC));
    ASSERT_THAT(searchResults, NotNull()) << "serach() returned a nullptr";
    EXPECT_THAT(toNamesVector(*searchResults), ContainsInOrder(expected))
        << "Unexpected sorting for ORDER_SIZE_DESC";

    // By creation time
    expected = {"testFile1", "Dir1", "testFile3", "Dir11", "testFile5", "testFile6"};
    searchResults.reset(megaApi[0]->search(filteringInfo.get(), MegaApi::ORDER_CREATION_ASC));
    ASSERT_THAT(searchResults, NotNull()) << "serach() returned a nullptr";
    EXPECT_THAT(toNamesVector(*searchResults), ContainsInOrder(expected))
        << "Unexpected sorting for ORDER_CREATION_ASC";

    // By creation inverted
    std::reverse(expected.begin(), expected.end());
    searchResults.reset(megaApi[0]->search(filteringInfo.get(), MegaApi::ORDER_CREATION_DESC));
    ASSERT_THAT(searchResults, NotNull()) << "serach() returned a nullptr";
    EXPECT_THAT(toNamesVector(*searchResults), ContainsInOrder(expected))
        << "Unexpected sorting for ORDER_CREATION_DES";

    // By modification time
    expected = {
        "testFile3", // 500 s ago
        "testFile6", // 300 s ago
        "testFile5", // 200 s ago
        "testFile2", // 100 s ago
        "testFile1", // Undef (upload time)
    };
    searchResults.reset(megaApi[0]->search(filteringInfo.get(), MegaApi::ORDER_MODIFICATION_ASC));
    ASSERT_THAT(searchResults, NotNull()) << "serach() returned a nullptr";
    EXPECT_THAT(toNamesVector(*searchResults), ContainsInOrder(expected))
        << "Unexpected sorting for ORDER_MODIFICATION_ASC";

    // By modification inverted
    std::reverse(expected.begin(), expected.end());
    searchResults.reset(megaApi[0]->search(filteringInfo.get(), MegaApi::ORDER_MODIFICATION_DESC));
    ASSERT_THAT(searchResults, NotNull()) << "serach() returned a nullptr";
    EXPECT_THAT(toNamesVector(*searchResults), ContainsInOrder(expected))
        << "Unexpected sorting for ORDER_MODIFICATION_DES";

    // By label, dirs first
    expected = {
        "Dir1", // Purple (6)
        "Dir2", // Nothing
        "testFile5", // Blue (5)
        "testFile3", // Yellow (3)
        "testFile2", // Orange (2)
        "testFile1", // Red (1)
        "testFile6" // Nothing
    };
    searchResults.reset(megaApi[0]->search(filteringInfo.get(), MegaApi::ORDER_LABEL_ASC));
    ASSERT_THAT(searchResults, NotNull()) << "serach() returned a nullptr";
    EXPECT_THAT(toNamesVector(*searchResults), ContainsInOrder(expected))
        << "Unexpected sorting for ORDER_LABEL_ASC";

    // By label inverted, dirs first
    expected = {
        "Dir2", // Nothing
        "Dir1", // Purple (6)
        "testFile6", // Nothing
        "testFile1", // Red (1)
        "testFile2", // Orange (2)
        "testFile3", // Yellow (3)
        "testFile5", // Blue (5)
    };
    searchResults.reset(megaApi[0]->search(filteringInfo.get(), MegaApi::ORDER_LABEL_DESC));
    ASSERT_THAT(searchResults, NotNull()) << "serach() returned a nullptr";
    EXPECT_THAT(toNamesVector(*searchResults), ContainsInOrder(expected))
        << "Unexpected sorting for ORDER_LABEL_DESC";

    // By fav, dirs first
    expected = {
        "Dir1", // fav
        "Dir2", // not fav
        "testFile6", // fav
        "testFile1", // not fav
    };
    searchResults.reset(megaApi[0]->search(filteringInfo.get(), MegaApi::ORDER_FAV_ASC));
    ASSERT_THAT(searchResults, NotNull()) << "serach() returned a nullptr";
    EXPECT_THAT(toNamesVector(*searchResults), ContainsInOrder(expected))
        << "Unexpected sorting for ORDER_FAV_ASC";

    // By fav inverted, dirs first
    expected = {
        "Dir2", // not fav
        "Dir1", // fav
        "testFile1", // not fav
        "testFile6", // fav
    };
    searchResults.reset(megaApi[0]->search(filteringInfo.get(), MegaApi::ORDER_FAV_DESC));
    ASSERT_THAT(searchResults, NotNull()) << "serach() returned a nullptr";
    EXPECT_THAT(toNamesVector(*searchResults), ContainsInOrder(expected))
        << "Unexpected sorting for ORDER_FAV_DESC";
}
