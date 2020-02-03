#if defined(ESP8266) || defined(ESP32)

#include "fs.h"
#include "esp_vfs.h"
#include <vfs_api.h>
#include <string.h>

namespace fs {

static int num_blobs_mounted = 0;

BlobFS::BlobFS()
 : FS(FSImplPtr(new VFSImpl()))
{}

BlobFS::BlobFS(const void* blob, const char* basePath)
 : FS(FSImplPtr(new VFSImpl()))
{
    if (!begin(blob, basePath)) {
        log_e("Failed to initialize fs::BlobFS instance");
        abort();
    }
}

BlobFS::~BlobFS() {
    end();
}

bool BlobFS::begin(const void* blob, const char* basePath) {
    end();

    char* mountpoint;
    if (basePath) {
        mountpoint = strdup(basePath);
    } else {
        // auto-generated base path: /blobfs-xxx
        mountpoint = (char*)malloc(16);
        snprintf(mountpoint, 16, "/blobfs-%d", ++num_blobs_mounted);
    }

    _blobfs = new blobfs::MemoryBlobFS(blob);
    esp_err_t err = blobfs::vfs_blobfs_register(mountpoint, *_blobfs);
    if (err != ESP_OK) {
        delete _blobfs;
        _blobfs = nullptr;
        free(mountpoint);
        return false;
    }
    _impl->mountpoint(mountpoint);
    return true;
}

void BlobFS::end() {
    if (_blobfs) {
        blobfs::vfs_blobfs_unregister(_impl->mountpoint());

        delete _blobfs;
        _blobfs = nullptr;
        free((char*)_impl->mountpoint());
        _impl->mountpoint(nullptr);
    }
}

} // namespace fs
#endif // ifdef esp32
