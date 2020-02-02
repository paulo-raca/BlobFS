#include "blobfs.h"
#include <cstring>
#include <cstdlib>
#include <cstddef>


namespace blobfs {
    // ================= Fix byte-order on data structures loaded from the blob =================

    static inline uint32_t ntohl(uint32_t n) {
#if ((__BYTE_ORDER__) == (__ORDER_BIG_ENDIAN__))
        return data;
#else
        return ((n & 0xff) << 24) | ((n & 0xff00) << 8) | ((n >> 8)  & 0xff00) | ((n >> 24) & 0xff);
#endif
    }

    static inline void fix_endianess(uint32_t &data) {
        data = ntohl(data);
    }
    static inline void fix_endianess(inode_data_t &data) {
        data.data_size = ntohl(data.data_size);
        data.data_offset = ntohl(data.data_offset);
    }
    static inline void fix_endianess(dir_entry_t &data) {
        data.name_offset = ntohl(data.name_offset);
        data.inode_data.data_size = ntohl(data.inode_data.data_size);
        data.inode_data.data_offset = ntohl(data.inode_data.data_offset);
    }




    // ================= Uncompressed File Handle =================

    class UncompressedFileHandle : public FileHandle {
        uint32_t _position;
    public:
        inline UncompressedFileHandle(BlobFS& blobfs, inode_data_t inode_data, inode_t inode)
        : FileHandle(blobfs, inode_data, inode), _position(0)
        {}


        virtual int tell(uint32_t& position) {
            position = _position;
            return 0;
        }

        virtual int seek(uint32_t position)  {
            if (_position > _inode_data.data_size) {
                return EINVAL;
            }
            _position = position;
            return 0;
        }

        virtual int read(void *dest, uint32_t &size) {
            int ret = pread(dest, size, _position);
            if (ret == 0) {
                _position += size; // On success, move file cursor
            }
            return ret;
        }

        virtual int pread(void *dest, uint32_t &size, uint32_t position) {
            // Return empty buffer on EOF
            if (position >= _inode_data.data_size) {
                size = 0;
                return 0;
            }

            // Trim the buffer if we are near EOF
            uint32_t remaining = _inode_data.data_size - position;
            if (size > remaining) {
                size = remaining;
            }

            // Perform the actual read
            return _blobfs.load_chunk(dest, _inode_data.data_offset + position, size);
        }
    };




    // ================= Directory Handle =================

    int DirHandle::readdir(dir_entry_t& direntry, inode_t &inode) {
        if (_position >= _inode_data.data_size) {
            return ENOENT;
        }
        offset_t entry_offset = _inode_data.data_offset + (_position++) * sizeof(dir_entry_t);
        inode = entry_offset + offsetof(dir_entry_t, inode_data);

        int ret = _blobfs.load_chunk(&direntry, entry_offset, sizeof(dir_entry_t));
        if (ret) {
            return ret;
        }
        fix_endianess(direntry);

        return 0;
    }




    // ================= Main FS functions =================

    int BlobFS::lookup_child(inode_t &child, inode_t parent_inode, const char* name) {
        inode_data_t parent;
        int ret = load_chunk(&parent, parent_inode, sizeof(inode_data_t));
        if (ret) {
            return ret;
        }
        fix_endianess(parent);

        if ((parent.flags & FLAG_DIR) == 0) {
            // We cannot lookup into a file, only into directories
            return ENOTDIR;
        }
        if ((parent.flags & FLAG_DEFLATE) != 0) {
            // Compression is not supported on directory indexes
            return ENOSYS;
        }

        //TODO: Use binary search instead

        offset_t current_direntry_ptr = parent.data_offset;
        for (uint32_t child_index = 0; child_index < parent.data_size; child_index++) {
            offset_t child_name_offset;
            ret = load_chunk(&child_name_offset, current_direntry_ptr + offsetof(dir_entry_t, name_offset), sizeof(offset_t)); // Don't need
            if (ret) {
                return ret;
            }
            fix_endianess(child_name_offset);

            const char* child_name;
            ret = load_str(child_name, child_name_offset);
            if (ret) {
                return ret;
            }

            int cmp = strcmp(name, child_name);
            free_str(child_name);


            // Found a matching name
            if (cmp == 0) {
                child = current_direntry_ptr + offsetof(dir_entry_t, inode_data);
                return 0;
            }

            // Go to the next entry
            current_direntry_ptr += sizeof(dir_entry_t);
        }

        // Not found
        return ENOENT;
    }

    int BlobFS::lookup(inode_t &inode, const char* path) {
        inode = 0;  // start from root inode

        // Path must start with "/"
        if (path == nullptr || path[0] != '/') {
            return ENOENT;
        }

        const char* chunk_start = path + 1;
        for (const char* chunk_end=chunk_start; ; chunk_end++) {
            char endchar = *chunk_end;
            if ((endchar == '/') || (endchar == '\0')) {
                if (chunk_end != chunk_start) { // Ignore empty chunks -- .e.g "/foo//bar/" == "/foo/bar"
                    size_t chunk_size = chunk_end - chunk_start;
                    char* chunk_name = (char*)malloc(chunk_size + 1);
                    memcpy(chunk_name, chunk_start, chunk_size);
                    chunk_name[chunk_size] = '\0';

                    int ret = lookup_child(inode, inode, chunk_name);
                    free(chunk_name);

                    if (ret) {
                        return ret;
                    }
                }
                chunk_start = chunk_end + 1;
            }
            if (endchar == '\0') {
                break;
            }
        }

        return 0;
    }

    int BlobFS::stat(inode_t inode, inode_data_t &inode_data) {
        int ret = load_chunk(&inode_data, inode, sizeof(inode_data_t));
        if (ret) {
            return ret;
        }
        fix_endianess(inode_data);
        return 0;
    }

    int BlobFS::open(FileHandle* &file, inode_t inode) {
        inode_data_t inode_data;
        int ret = load_chunk(&inode_data, inode, sizeof(inode_data_t));
        if (ret) {
            return ret;
        }
        fix_endianess(inode_data);

        if ((inode_data.flags & FLAG_DIR) != 0) {
            // open only takes regular files
            return EISDIR;
        }

        file = new UncompressedFileHandle(*this, inode_data, inode);
        return 0;
    }

    int BlobFS::opendir(DirHandle* &dir, inode_t inode) {
        inode_data_t inode_data;
        int ret = load_chunk(&inode_data, inode, sizeof(inode_data_t));
        if (ret) {
            return ret;
        }
        fix_endianess(inode_data);

        if ((inode_data.flags & FLAG_DIR) == 0) {
            // opendir only takes directories
            return ENOTDIR;
        }

        dir = new DirHandle(*this, inode_data, inode);
        return 0;
    }




    // ================= Memory-mapped BlobFS =================

    MemoryBlobFS::MemoryBlobFS(const void* blob)
    : _blob(blob)
    {}

    int MemoryBlobFS::load_chunk(void* dest, uint32_t offset, uint32_t len) {
        memcpy(dest, (char*)this->_blob + offset, len);
        return 0;
    }

    int MemoryBlobFS::load_str(const char* &str, offset_t offset) {
        str = (const char*)this->_blob + offset;
        return 0;
    }

    void MemoryBlobFS::free_str(const char* str) {
        //No-op, str is a direct pointer to the blob
    }
}
