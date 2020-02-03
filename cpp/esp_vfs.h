# pragma once

#if !defined(ESP8266) && !defined(ESP32)
#error <blobfs/esp_vfs.h> is only enabled on ESP8266 and ESP32
#endif

#include "blobfs.h"
#include <esp_err.h>

namespace blobfs {
    esp_err_t vfs_blobfs_register(const char* base_path, BlobFS& fs);
    esp_err_t vfs_blobfs_unregister(const char* base_path);
}
