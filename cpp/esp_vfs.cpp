#if defined(ESP8266) || defined(ESP32)

#include <Arduino.h>
#include "esp_vfs.h"
#include <esp_vfs.h>
#include <sys/errno.h>
#include <fcntl.h>
#include <sys/stat.h>

using namespace blobfs;

static inline BlobFS* ctx_to_blobfs(void* ctx) {
    return (BlobFS*)ctx;
}

//FIXME: locking
static FileHandle** _file_handles = nullptr;
static int _n_file_handles = 0;

static inline FileHandle* fd_to_fh(void* fs, int fd) {
    if ((fd < 0) || (fd >= _n_file_handles)) {
        return nullptr;
    }
    return _file_handles[fd];
}

static inline int register_fd(BlobFS* fs, FileHandle* fh) {
    for (int i=0; i<_n_file_handles; i++) {
        if (_file_handles[i] == nullptr) {
            _file_handles[i] = fh;
            return i;
        }
    }
    int old_n = _n_file_handles;
    _n_file_handles = old_n * 2;
    if (_n_file_handles < 10) {
        _n_file_handles = 10;
    }
    _file_handles = (FileHandle**)realloc(_file_handles, _n_file_handles * sizeof(FileHandle*));
    for (int i=old_n; i<_n_file_handles; i++) {
        _file_handles[i] = nullptr;
    }
    _file_handles[old_n] = fh;
    return old_n;
}

static inline void release_fd(BlobFS* fs, int fd) {
    _file_handles[fd] = nullptr;
}

static inline void translate_stat(inode_data_t &inode_data, inode_t inode, struct stat *st) {
    memset(st, 0, sizeof(struct stat));
    st->st_ino = inode;
    st->st_size = inode_data.data_size;
    st->st_mode = ((inode_data.flags & FLAG_DIR) ? S_IFDIR : S_IFREG) | S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
}

static const esp_vfs_t vfs_blobfs_ops = []() {
    esp_vfs_t ops{};
    ops.flags = ESP_VFS_FLAG_CONTEXT_PTR;

    // =========================================================================
    // File handling
    ops.lseek_p = [](void* ctx, int fd, off_t offset, int mode) -> off_t {
        FileHandle* fh = fd_to_fh(ctx, fd);

        switch (mode) {
            case SEEK_SET: {
                break; // Ok
            }
            case SEEK_CUR: {
                uint32_t cur_pos;
                int ret = fh->tell(cur_pos);
                if (ret) {
                    errno = ret;
                    return -1;
                }
                offset += cur_pos;
                break;
            }
            case SEEK_END: {
                uint32_t size;
                int ret = fh->size(size);
                if (ret) {
                    errno = ret;
                    return -1;
                }
                offset += size;
                break;
            }
        }

        if (offset < 0) {
            errno = EINVAL;
            return -1;
        }

        int ret = fh->seek(offset);
        if (ret) {
            errno = ret;
            return -1;
        }
        return offset;
    };
    ops.read_p = [](void* ctx, int fd, void * dst, size_t size) {
        FileHandle* fh = fd_to_fh(ctx, fd);
        if (fh == nullptr) {
            errno = EBADF;
            return -1;
        }
        uint32_t n = size;
        int ret = fh->read(dst, n);
        if (ret) {
            errno = ret;
            return -1;
        }
        return (int)n;
    };
//     ops.pread_p = [](void *ctx, int fd, void * dst, size_t size, off_t offset) {
//         FileHandle* fh = fd_to_fh(ctx, fd);
//         if (fh == nullptr) {
//             errno = EBADF;
//             return -1;
//         }
//         uint32_t n = size;
//         int ret = fh->pread(dst, n, offset);
//         if (ret) {
//             errno = ret;
//             return -1;
//         }
//         return (int)n;
//     };
    ops.open_p = [](void* ctx, const char * path, int flags, int mode) {
        Serial.printf("BlobFS.open('%s', %d)\n", path, flags);

        // Can only open for read
        if (flags & O_WRONLY) {
            errno = EROFS;
            return -1;
        }

        BlobFS* blobfs = ctx_to_blobfs(ctx);
        FileHandle* fh;
        int ret = blobfs->open(fh, path);
        int open(FileHandle* &file, inode_t inode);
        if (ret) {
            errno = ret;
            return -1;
        }
        return register_fd(blobfs, fh);
    };
    ops.close_p = [](void* ctx, int fd) {
        BlobFS* blobfs = ctx_to_blobfs(ctx);
        FileHandle* fh = fd_to_fh(ctx, fd);
        if (fh == nullptr) {
            errno = EBADF;
            return -1;
        }
        release_fd(blobfs, fd);
        return 0;
    };
    ops.fstat_p = [](void* ctx, int fd, struct stat * st) {
        FileHandle* fh = fd_to_fh(ctx, fd);
        if (fh == nullptr) {
            errno = EBADF;
            return -1;
        }
        inode_data_t inode_data;
        inode_t inode;
        int ret = fh->stat(inode_data, inode);
        if (ret) {
            errno = ret;
            return -1;
        }
        translate_stat(inode_data, inode, st);
        return 0;
    };

    // =========================================================================
    // Directory handling


    // =========================================================================
    // Ops that I don't need to implement because they are no-ops
//     ops.fcntl_p = [](void* ctx, int fd, int cmd, int arg) {}
//     ops.ioctl_p = [](void* ctx, int fd, int cmd, va_list args) {}
    ops.fsync_p = [](void* ctx, int fd) {
        return 0;  // Sync is a no-op in a read-only FS
    };
    ops.access_p = [](void* ctx, const char *path, int amode) {
        // Check mode
        if (amode & W_OK) {
            errno = EROFS;
            return -1;
        }
        // Check if file exists
        BlobFS* blobfs = ctx_to_blobfs(ctx);
        inode_t inode;
        int ret = blobfs->lookup(inode, path);
        if (ret) {
            errno = ret;
            return -1;
        }
        return 0;
    };
    ops.stat_p = [](void* ctx, const char * path, struct stat * st) {
        BlobFS* blobfs = ctx_to_blobfs(ctx);
        inode_data_t inode_data;
        inode_t inode;
        int ret = blobfs->stat(inode_data, inode, path);
        if (ret) {
            errno = ret;
            return -1;
        }
        translate_stat(inode_data, inode, st);
        return 0;
    };

    // =========================================================================
    // Write operations that must fail with EROFS
    ops.write_p = [](void *ctx, int fd, const void *src, size_t size) {
        errno = EROFS;
        return -1;
    };
//     ops.pwrite_p = [](void *ctx, int fd, const void *src, size_t size, off_t offset) {
//         errno = EROFS;
//         return -1;
//     };

    ops.link_p = [](void* ctx, const char* n1, const char* n2) {
        errno = EROFS;
        return -1;
    };
    ops.unlink_p = [](void* ctx, const char *path) {
        errno = EROFS;
        return -1;
    };
    ops.rename_p = [](void* ctx, const char *src, const char *dst) {
        errno = EROFS;
        return -1;
    };
    ops.mkdir_p = [](void* ctx, const char* name, mode_t mode) {
        errno = EROFS;
        return -1;
    };
    ops.rmdir_p = [](void* ctx, const char* name) {
        errno = EROFS;
        return -1;
    };
    ops.truncate_p = [](void* ctx, const char *path, off_t length) {
        errno = EROFS;
        return -1;
    };
//     ops.utime_p = [](void* ctx, const char *path, const struct utimbuf *times) {
//         errno = EROFS;
//         return -1;
//     };

  return ops;
}();



esp_err_t blobfs::vfs_blobfs_register(const char* base_path, BlobFS& fs) {
    return esp_vfs_register(base_path, &vfs_blobfs_ops, &fs);
}
esp_err_t blobfs::vfs_blobfs_unregister(const char* base_path) {
    return esp_vfs_unregister(base_path);
}

#endif // ifdef esp32
