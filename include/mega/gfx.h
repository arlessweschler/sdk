/**
 * @file gfx.h
 * @brief Bitmap graphics processing
 *
 * (c) 2014 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */

#ifndef GFX_H
#define GFX_H 1

#include <mutex>

#ifdef USE_IOS
#include "mega/posix/megawaiter.h"
#else
#include "megawaiter.h"
#endif
#include "mega/thread/posixthread.h"
#include "mega/thread/cppthread.h"

namespace mega {

class MEGA_API GfxJob
{
public:
    GfxJob();

    // locally encoded path of the image
    LocalPath localfilename;

    // vector with the required image type
    vector<fatype> imagetypes;

    // handle related to the image
    NodeOrUploadHandle h;

    // key related to the image
    byte key[SymmCipher::KEYLENGTH];

    // resulting images
    vector<string *> images;
};

class MEGA_API GfxJobQueue
{
    protected:
        std::deque<GfxJob *> jobs;
        std::mutex mutex;

    public:
        GfxJobQueue();
        void push(GfxJob *job);
        GfxJob *pop();
};

// Interface for graphic processor provider used by GfxProc
class MEGA_API IGfxProvider
{
public:
    struct Dimension final
    {
        Dimension(int w, int h) : width(w), height(h) {};

        Dimension() : width(0), height(0) {};

        int width;

        int height;
    };

    virtual ~IGfxProvider() = default;

    // It generates thumbnails for the file by localfilepath according to the dimensions
    // The function should always return the number of thumbnails as the dimensions. The
    // vector of empty strings should be return on error cases.
    virtual std::vector<std::string> generateImages(FileSystemAccess* fa,
                                                    const LocalPath& localfilepath,
                                                    const std::vector<Dimension>& dimensions) = 0;

    // list of supported extensions (NULL if no pre-filtering is needed)
    virtual const char* supportedformats() = 0;

    // list of supported video extensions (NULL if no pre-filtering is needed)
    virtual const char* supportedvideoformats() = 0;

    static std::unique_ptr<IGfxProvider> createInternalGfxProvider();
};

// Interface for the local graphic processor provider
// Implementations should be able to allocate/deallocate and manipulate bitmaps,
// No thread safety is required among the operations
class MEGA_API IGfxLocalProvider : public IGfxProvider
{
public: // read and store bitmap
    virtual ~IGfxLocalProvider() = default;

    virtual std::vector<std::string> generateImages(FileSystemAccess* fa,
                                                    const LocalPath& localfilepath,
                                                    const std::vector<Dimension>& dimensions) override;

private:
    virtual bool readbitmap(FileSystemAccess*, const LocalPath&, int) = 0;

    // resize stored bitmap and store result as JPEG
    virtual bool resizebitmap(int, int, string* result) = 0;

    // free stored bitmap
    virtual void freebitmap() = 0;

    int width() { return w; }
    int height() { return h; }

protected:
    // coordinate transformation
    static void transform(int&, int&, int&, int&, int&, int&);

    int w, h;
};

// bitmap graphics processor
class MEGA_API GfxProc
{
    bool finished;
    WAIT_CLASS waiter;
    std::mutex mutex;
    THREAD_CLASS thread;
    bool threadstarted = false;
    SymmCipher mCheckEventsKey;
    GfxJobQueue requests;
    GfxJobQueue responses;

    class ProviderAccessor
    {
    public:
        ProviderAccessor(std::unique_ptr<IGfxProvider> provider) : mProvider(std::move(provider)) {}

        std::shared_ptr<IGfxProvider> getCopy() const;

        void set(std::unique_ptr<IGfxProvider> provider);
    private:
        std::shared_ptr<IGfxProvider>  mProvider;
        mutable std::mutex  mMutex;
    };

    ProviderAccessor mGfxProvider;

    static void *threadEntryPoint(void *param);
    void loop();

    std::vector<IGfxProvider::Dimension> getJobDimensions(GfxJob *job);

    // Caller should give dimensions from high resolution to low resolution
    std::vector<std::string> generateImages(const LocalPath& localfilepath, const std::vector<IGfxProvider::Dimension>& dimensions);

    std::string generateOneImage(const LocalPath& localfilepath, const IGfxProvider::Dimension& dimension);

public:
    // synchronously processes the results of gendimensionsputfa() (if any) in a thread safe manner
    int checkevents(Waiter*);

    // synchronously check whether the filename looks like a supported media type
    bool isgfx(const LocalPath&);

    // synchronously check whether the filename looks like a video
    bool isvideo(const LocalPath&);

    // synchronously generate all dimensions and returns the count
    // asynchronously write to metadata server and attach to PUT transfer or existing node,
    // upon finalization the job is stored in responses object in a thread safe manner, and client waiter is notified
    // The results can be processed by calling checkevents()
    // handle is uploadhandle or nodehandle
    // - must respect JPEG EXIF rotation tag
    // - must save at 85% quality (120*120 pixel result: ~4 KB)
    int gendimensionsputfa(FileAccess*, const LocalPath&, NodeOrUploadHandle, SymmCipher*, int missingattr);

    // FIXME: read dynamically from API server
    typedef enum { THUMBNAIL, PREVIEW } meta_t;
    typedef enum { AVATAR250X250 } avatar_t;

    // synchronously generate and save a fa to a file
    bool savefa(const LocalPath& source, const IGfxProvider::Dimension& dimension, LocalPath& destination);

    // - w*0: largest square crop at the center (landscape) or at 1/6 of the height above center (portrait)
    // - w*h: resize to fit inside w*h bounding box
    static const std::vector<IGfxProvider::Dimension> DIMENSIONS;
    static const std::vector<IGfxProvider::Dimension> DIMENSIONS_AVATAR;

    MegaClient* client;

    // start a thread that will do the processing
    void startProcessingThread();

    // Please note that changing the gfx settings at runtime while the gfx system is
    // in use can lead to a race condition:
    // - A call to isGfx uses the old provider.
    // - A subsequent call to generateOneImage uses the new provider, which may not support the same image format.
    // This can cause the generateOneImage function to fail. Only utilize this interface if you can tolerate temporary failures.
    void setGfxProvider(std::unique_ptr<IGfxProvider> provider);

    // The provided IGfxProvider implements library specific image processing
    // Thread safety among IGfxProvider methods is guaranteed by GfxProc
    GfxProc(std::unique_ptr<IGfxProvider>);
    virtual ~GfxProc();
};
} // namespace

#endif
