BlobFS
======

On embedded systems, it is often useful to store a read-only, compressed and efficient filesystem.

On linux world, this is often achieved by SquashFS, but it is maybe too complex for a microcontroller environment.

Hence BlobFS is born: You whole filesystem can be stored as a blob in a local variable, and a simple library
can provides the usual filesystem APIs. It only supports files and directories, and

Format
======

Inode Entry:
- flags: IS_DIR / IS_FILE, DEFLATE
- length: number of bytes for files (Uncompressed), number of entries for directories
- pointer: Pointer to file contents, or to directory contents

Directory contents:
There is a sequence of `entry.size` records, ordered by name, each one containing:

- name: Pointer to name string, NULL-terminated
- entry: An inlined inode entry

The root inode entry (`/`) should be placed at offset 0, all other pointers are relative to the start of the blob.
