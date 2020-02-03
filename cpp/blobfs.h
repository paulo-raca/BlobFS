# pragma once
#include <cinttypes>
#include <sys/errno.h>

namespace blobfs {
    /** An offset (pointer) within the blob */
    typedef uint32_t offset_t;

    /**
     * An inode identifier
     * It is actually the offset of a `inode_data_t` inside the blob
     *
     * The root inode has offset 0
     */
    typedef offset_t inode_t;

    /** An inode_data_t with this flag represents a folder -- Otherwise it is a regular file */
    constexpr uint8_t FLAG_DIR = 1;

    /** inode_data_t with this flag represents a file whose contents are compressed with zlib -- Only valid for regular files! */
    constexpr uint8_t FLAG_DEFLATE = 2;

    /** An inode data */
    typedef struct {
        /** Size of a regular file (Uncompressed), or number of entries in a directory */
        uint32_t data_size;
        /** Offset of the contents of regular file, or offset to entries (dir_entry_t[data_size]) in a directory */
        offset_t data_offset;
        /** Inode flags: FLAG_DIR, FLAG_DEFLATE */
        uint8_t flags;
    } __attribute__((packed)) inode_data_t;

    /** Entry of a directory */
    typedef struct {
        /** Offset of the file name, which must be a NULL-terminated string withing the blob */
        offset_t name_offset;
        /** The inode data */
        inode_data_t inode_data;
    } __attribute__((packed)) dir_entry_t;


    class BlobFS;
    class FileHandle;
    class UncompressedFileHandle;
    class CompressedFileHandle;
    class DirHandle;

    /**
     * HAL used to access a chunk of the blob
     *
     * memory-mapped implementations will just return the appropriate pointer on `load_chunk` and do nothing on `load_chunk`.
     *
     */
    class BlobFS {
    public:
        /**
         * Lookup an inode from an absolute path
         *
         * @param[out] child Address of the inode, if found
         * @param[in] name Full path to the inode being looked up
         * @return 0 on success, or errno
         */
        int lookup(inode_t &inode, const char* path);

        /**
         * Lookup a child inode by name
         *
         * @param[out] child Address of the child, if found
         * @param[in] parent Address of the parent inode, where the child is being looked up
         * @param[in] name Name of the child being looked up
         * @return 0 on success, or errno
         */
        int lookup_child(inode_t &child, inode_t parent_inode, const char* name);

        /**
         * Opens the directory for listing files
         *
         * After use, the directory handle must be released with `delete dir`
         *
         * @param[out] dir the directory handle.
         * @param[in] inode The inode of the directory
         * @return 0 on success, or errno
         */
        int opendir(DirHandle* &dir, inode_t inode);

        /**
         * Opens the directory for listing files
         *
         * After use, the directory handle must be released with `delete dir`
         *
         * @param[out] dir the directory handle.
         * @param[in] path The path of the directory in the filesystem
         * @return 0 on success, or errno
         */
        inline int opendir(DirHandle* &dir, const char* path) {
            inode_t inode;
            int ret = lookup(inode, path);
            if (ret) {
                return ret;
            }
            return opendir(dir, inode);
        }

        /**
         * Opens a file for reading
         *
         * After use, the file handle must be released with `delete file`
         *
         * @param[out] file the file handle.
         * @param[in] inode The inode of the file
         * @return 0 on success, or errno
         */
        int open(FileHandle* &file, inode_t inode);

        /**
         * Opens a file for reading
         *
         * After use, the file handle must be released with `delete file`
         *
         * @param[out] file the file handle.
         * @param[in] path The path of the file in the filesystem
         * @return 0 on success, or errno
         */
        inline int open(FileHandle* &file, const char* path) {
            inode_t inode;
            int ret = lookup(inode, path);
            if (ret) {
                return ret;
            }
            return open(file, inode);
        }

        /**
         * Returns all the metadata of the specified inode
         *
         * @param[out] inode_data metadata of the specified inode
         * @param[in] inode The inode number being queried
         * @return 0 on success, or errno
         */
        int stat(inode_data_t &inode_data, inode_t inode);

        /**
         * Returns all the metadata of the specified inode
         *
         * @param[out] inode_data metadata of the specified inode
         * @param[out] inode The inode number associated with the path
         * @param[in] path The file path being queried
         * @return 0 on success, or errno
         */
        inline int stat(inode_data_t &inode_data, inode_t &inode, const char* path) {
            int ret = lookup(inode, path);
            if (ret) {
                return ret;
            }
            return stat(inode_data, inode);
        }

    protected:
        friend class FileHandle;
        friend class CompressedFileHandle;
        friend class UncompressedFileHandle;
        friend class DirHandle;

        // ==== HAL used to access a chunks of the blob ====/

        /**
         * Loads a chunk of the blob in local memory
         *
         * @param[out] dest buffer chunk will be copied to
         * @param[in] offset Offset at the blob where the chunk starts
         * @param[in] len Size of the chunk
         * @return 0 on success, or errno
         */
        virtual int load_chunk(void* dest, offset_t offset, uint32_t len) = 0;

        /**
         * Loads a NULL_TERMINATED string in the local memory
         *
         * Since the string size is unknown ahead of time, this function might
         * allocate a buffer if necessary, and the caller must call free_str_chunk() once the string has
         * been used.
         *
         * @param[out] str Will point to a buffer with the requested string
         * @param[in] offset Offset at the blob where the string starts
         * @return 0 on success, or errno
         */
        virtual int load_str(const char* &str, offset_t offset) = 0;

        /**
         * Frees a strings returned by load_str_chunk
         */
        virtual void free_str(const char* str) = 0;
    };

    class FileHandle {
    protected:
        BlobFS& _blobfs;
        inode_data_t _inode_data;
        inode_t _inode;

    public:
        inline FileHandle(BlobFS& blobfs, inode_data_t inode_data, inode_t inode)
        : _blobfs(blobfs), _inode_data(inode_data), _inode(inode)
        {}

        /**
         * Returns all the metadata of the current inode
         *
         * @param[out] inode_data metadata of the current inode
         * @param[out] inode The inode number of the current file
         * @return 0 on success, or errno
         */
        inline int stat(inode_data_t &inode_data, inode_t &inode) {
            inode_data = _inode_data;
            inode = _inode;
            return 0;
        }

        /**
         * Returns the size of this file
         *
         * @param[out] size The file size
         * @return 0 on success, or errno
         */
        inline int size(uint32_t& size) {
            size = _inode_data.data_size;
            return 0;
        }

        /**
         * Returns the current position of the file cursor
         *
         * @param[out] position The position of the cursor
         * @return 0 on success, or errno
         */
        virtual int tell(uint32_t& position) = 0;

        /**
         * Moves to file cursor the specified position
         *
         * @param[in] position The new position of the cursor
         * @return 0 on success, or errno
         */
        virtual int seek(uint32_t position) = 0;

        /**
         * Reads up to `size` bytes into the buffer from the file's current cursor position
         *
         * @param[out] dest Buffer to be filled with file contents
         * @param[in,out] size Input: Size of the `dest` buffer; Output: number of bytes actually read
         * @return 0 on success, or errno
         */
        virtual int read(void *dest, uint32_t &size) = 0;

        /**
         * Reads up to `size` bytes into the buffer from the specified file position
         *
         * @param[out] dest Buffer to be filled with file contents
         * @param[in,out] size Input: Size of the `dest` buffer; Output: number of bytes actually read
         * @param[in] position Position on the file being read
         * @return 0 on success, or errno
         */
        virtual int pread(void *dest, uint32_t &size, uint32_t position) = 0;
    };

    /**
     * Represents an open directory, used for listing its contents
     *
     * When done, use release the instance with `delete`
     */
    class DirHandle {
    protected:
        BlobFS& _blobfs;
        inode_data_t _inode_data;
        inode_t _inode;
        uint32_t _position;

    public:
        inline DirHandle(BlobFS& blobfs, inode_data_t inode_data, inode_t inode)
        : _blobfs(blobfs), _inode(inode), _inode_data(inode_data), _position(0)
        {}

        /**
         * Returns all the metadata of the current inode
         *
         * @param[out] inode_data metadata of the current inode
         * @param[out] inode The inode number of the current file
         * @return 0 on success, or errno
         */
        inline int stat(inode_data_t &inode_data, inode_t &inode) {
            inode_data = _inode_data;
            inode = _inode;
            return 0;
        }

        /**
         * Returns the number of entries in the directory listing
         *
         * @param[out] size The number of entries
         * @return 0 on success, or errno
         */
        inline int size(uint32_t& size) {
            size = _inode_data.data_size;
            return 0;
        }

        /**
         * Returns the current position at the directory listing
         *
         * @param[out] position The index of the next entry
         * @return 0 on success, or errno
         */
        inline int tell(uint32_t& position) {
            position = _position;
            return 0;
        }

        /**
         * Moves to the specified position in the directory listing
         *
         * @param[in] position The index of the next entry
         * @return 0 on success, or errno
         */
        inline int seek(uint32_t position) {
            if (position > _inode_data.data_size) {
                return EINVAL;
            }
            _position = position;
            return 0;
        }

        /**
         * Reads the next entry in this directory
         *
         * @param[out] direntry The data associated with the entry
         * @param[out] inode The inode associated with the entry
         * @return 0 on success, ENOENT if it reached the end of the list of entries, or errno
         */
        int readdir(dir_entry_t& direntry, inode_t &inode);

        /**
         * Convenience method for reading the next entry in this directory and
         * returning its name
         *
         * @param[out] direntry The data associated with the entry
         * @param[out] inode The inode associated with the entry
         * @param[out] name Name of the entry, must be released with `blobfs->free_str(name)`
         * @return 0 on success, ENOENT if it reached the end of the list of entries, or errno
         */
        int readdir(dir_entry_t& direntry, inode_t &inode, const char* &name) {
            int ret = readdir(direntry, inode);
            if (ret) {
                return ret;
            }
            return _blobfs.load_str(name, direntry.name_offset);
        }
    };

    /**
     * The simplest BlobFS implementation, stores the blob in a RAM pointer that can be accessed directly
     */
    class MemoryBlobFS : public BlobFS {
    protected:
        const void* _blob;
    public:
        MemoryBlobFS(const void* blob);
        virtual int load_chunk(void* dest, uint32_t offset, uint32_t len);
        virtual int load_str(const char* &str, offset_t offset);
        virtual void free_str(const char* str);
    };
}
