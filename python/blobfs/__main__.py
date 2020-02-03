from .impl import compile_path
import argparse
import watchdog

def main_create(src, dest, format='raw', watch=False, compress=False, prefix=None, sufix=None):
    def do_create():
        print("Creating BlobFS...")
        raw_blob = compile_path(src, compress=compress)

        if format == "raw":
            blob = raw_blob
        elif format == 'py':
            blob = bytes(repr(raw_blob), 'utf-8')
        elif format == 'c':
            c_escapes = {
                0x07: b"\\a",
                0x08: b"\\b",
                0x09: b"\\t",
                0x0a: b"\\n",
                0x0b: b"\\v",
                0x0c: b"\\f",
                0x0d: b"\\r",
                0x22: b'\\"',
                0x27: b"\\'",
                0x5c: b"\\\\",
            }
            def escape_char(c):
                if c in c_escapes:
                    return c_escapes[c]
                if c >= 32 and c <=127:
                    return bytes([c])
                return bytes(f"\\{c:0o}", "utf-8")
            blob = b'"' + b''.join([escape_char(c) for c in raw_blob]) + b'"'

        with open(dest, "wb") as f:
            if prefix:
                f.write(bytes(prefix, 'utf-8'))
            f.write(blob)
            if sufix:
                f.write(bytes(sufix, 'utf-8'))

        print(f"BlobFS created at {dest}, size={len(raw_blob)}")


    do_create()

    if watch:
        class handler(watchdog.events.FileSystemEventHandler):
            def on_any_event(self, event):
                do_create()

        observer =  watchdog.observers.Observer()
        observer.schedule(handler(), src, recursive=True)
        observer.start()
        try:
            observer.join()
        except KeyboardInterrupt:
            observer.stop()
        finally:
            observer.join()


parser = argparse.ArgumentParser(description="BlobFS CLI")
subparsers = parser.add_subparsers(dest='cmd', required=True)
cmds = {}

create_parser = subparsers.add_parser("create", help="Creates BlobFS blob from a path")
create_parser.add_argument("src", metavar="SRC",
                          help="Path used as a source to the BlobFS")
create_parser.add_argument("dest", metavar="DEST",
                          help="Destination directory")
create_parser.add_argument("--format", metavar="FORMAT", default="raw", choices=["raw", "c", "py"],
                          help="How to encode the blob")
create_parser.add_argument("--watch", action="store_true", help="Watch for FS changes")
create_parser.add_argument("--compress", action="store_true", help="Enable file compression")
create_parser.add_argument("--prefix", help="store a prefix to the file")
create_parser.add_argument("--sufix", help="store a sufix to the file")
cmds["create"] = main_create

args = vars(parser.parse_args())
cmds[args.pop("cmd")](**args)
