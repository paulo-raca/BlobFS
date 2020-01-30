import io 
import os
from enum import IntFlag
import struct
import zlib
import time
from watchdog.observers import Observer
from watchdog.events import FileSystemEventHandler

PTR_SIZE = 4
ENTRY_SIZE = 1 + 2 * PTR_SIZE
DIRENTRY_SIZE = PTR_SIZE + ENTRY_SIZE

class InodeFlags(IntFlag):
    IS_DIR = 1
    DEFLATE = 2  # Only for files 


class BlobCompiler:
    def __init__(self, compress=False):
        self.blob = io.BytesIO()
        self.cache = {}
        self.compress = compress

    def store_data(self, data):
        # TODO: If data is a prefix of some entry already in the cache, that works too!
        if data not in self.cache:
            self.cache[data] = self.blob.seek(0, io.SEEK_END)
            self.blob.write(data)
            #print(f"Blob {data} written to {self.cache[data]}")
        return self.cache[data]
    
    def store_compressed_data(self, data):
        zdata = zlib.compress(data)
        
        if self.compress and len(zdata) < len(data):
            return self.store_data(zdata), InodeFlags.DEFLATE
        else: 
            #print(f"Storing {data} without compression")
            return self.store_data(data), 0
    
    def create_entry(self, entry):
        if isinstance(entry, dict):
            flags = InodeFlags.IS_DIR
            size = len(entry)
            
            entry_table = b''
            for child_name, child_entry in sorted(entry.items()):
                entry_table += struct.pack("<I", self.store_data(bytes(child_name, "utf-8") + b"\0"))
                entry_table += self.create_entry(child_entry)
            ptr = self.store_data(entry_table)
        else:
            if isinstance(entry, str):
                entry = bytes(entry, "utf-8")
            elif not isinstance(entry, bytes):
                raise Exception("Entry must be dict, str or bytes")
            
            size = len(entry)
            ptr, flags = self.store_compressed_data(entry)

        return struct.pack("<BII", flags, size, ptr)
    
    
    def compile(self, root):
        # Reserve space for root entry at offset zero
        self.blob.truncate(0)
        self.blob.seek(0)
        self.blob.write(b"x" * ENTRY_SIZE)
        
        root_entry = self.create_entry(root)
        self.blob.seek(0)
        self.blob.write(root_entry)
        return self.blob.getvalue()
    
class BlobLoader:
    def __init__(self, blob):
        self.blob = io.BytesIO(blob)
    
    def load_string(self, ptr):
        self.blob.seek(ptr)
        ret = b''
        while True:
            c = self.blob.read(1)
            if c == b'\0':
                break
            ret += c
        return str(ret, 'utf-8')
    
    def load_entry(self, ptr):
        self.blob.seek(ptr)
        data = self.blob.read(ENTRY_SIZE)
        flags, size, ptr = struct.unpack("<BII", data)
        
        if flags & InodeFlags.IS_DIR:
            ret = {}
            for i in range(size):
                self.blob.seek(ptr)
                nameptr, = struct.unpack("<I", self.blob.read(PTR_SIZE))
                name = self.load_string(nameptr)
                ptr += PTR_SIZE                
                ret[name] = self.load_entry(ptr)
                ptr += ENTRY_SIZE
            return ret
        else:
            self.blob.seek(ptr)
            if flags & InodeFlags.DEFLATE:
                with gzip.GzipFile(mode="rb", fileobj=self.blob) as stream:
                    return stream.read(size)
            else:
                return self.blob.read(size)
        
    @property
    def root(self):
        return self.load_entry(0)


def compile(data, compress=False):
    return BlobCompiler(compress=compress).compile(data)


def compile_path(path, compress=False):
    def path_to_data(path):
        if os.path.isfile(path):
            with open(path, 'rb') as f:
                return f.read()
        elif os.path.isdir(path):
            return {
                child: path_to_data(os.path.join(path, child))
                for child in os.listdir(path)
            }
        else:
            raise IOException(f"Invalid path: {path}")
    return compile(path_to_data(path), compress=compress)


def load(blob):
    return BlobLoader(blob).root
    
