import io 
import os
from enum import IntFlag
import struct
import gzip
import time
from watchdog.observers import Observer
from watchdog.events import FileSystemEventHandler

PTR_SIZE = 4
ENTRY_SIZE = 1 + 2 * PTR_SIZE
DIRENTRY_SIZE = PTR_SIZE + ENTRY_SIZE

class InodeFlags(IntFlag):
    IS_DIR = 1
    GZIPPED = 2  # Only for files 


class BlobCompiler:
    def __init__(self, compress=True):
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
        gzdata = gzip.compress(data)
        
        if self.compress and len(gzdata) < len(data):
            #print(f"Storing {data} with gz")
            return self.store_data(gzdata), InodeFlags.GZIPPED
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
            if flags & InodeFlags.GZIPPED:
                with gzip.GzipFile(mode="rb", fileobj=self.blob) as stream:
                    return stream.read(size)
            else:
                return self.blob.read(size)
        
    @property
    def root(self):
        return self.load_entry(0)

def file_to_data(filename):
    if os.path.isfile(filename):
        with open(filename, 'rb') as f:
            return f.read()
        
    elif os.path.isdir(filename):
        return {
            child: file_to_data(os.path.join(filename, child))
            for child in os.listdir(filename)
        }
            
    else:
        raise IOException(f"Invalid path: {filename}")



class handler(FileSystemEventHandler):
    def on_any_event(self, event):
        #data = {"Foo": "barsdf", "asdfsdfsfsdf": {"x": "dfdsfssdfsdfsdfdsfsdfsdfdfsdfsdfdsfdsfddfsds", "y":{}}, "x": {"x": "dfdsfsdfsds", "y":{}}}
        data = file_to_data("..")
        #print(data)
        blob = BlobCompiler().compile(data)
        print(f"Blob: {len(blob)} bytes") 
        data = BlobLoader(blob).root                 
        #print(data)

observer = Observer()
observer.schedule(handler(), ".", recursive=True)
observer.start()
try:
    while True:
        time.sleep(1)
except KeyboardInterrupt:
    observer.stop()
observer.join()
