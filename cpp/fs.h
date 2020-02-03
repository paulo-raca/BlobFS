# pragma once

#if !defined(ESP8266) && !defined(ESP32)
#error <blobfs/fs.h> is only enabled on ESP8266 and ESP32
#endif

#include <FS.h>
#include "blobfs.h"

namespace fs {

    class BlobFS : public FS {
    ::blobfs::BlobFS *_blobfs;
    public:
        BlobFS();
        BlobFS(const void* blob, const char* basePath=nullptr);
        ~BlobFS();
        bool begin(const void* blob, const char* basePath=nullptr);
        void end();
    };

}
