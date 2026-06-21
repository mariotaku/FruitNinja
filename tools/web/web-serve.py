#!/usr/bin/env python3
"""Dev server for the Emscripten web build with content-hash-aware caching.

Caching policy (matches the project's hashed-asset scheme from web-hash-assets.py):
  - HTML (index / *.html): `Cache-Control: no-store` -> ALWAYS refetched, so a new
    build's HTML (which points at freshly-hashed asset URLs) is picked up on a plain
    refresh. No more hard-refresh dance on the phone.
  - Content-hashed assets (`...-<hex>.{wasm,js,data,mem,png,...}`): immutable, cached
    one year -- a content change produces a NEW filename, so caching is always safe.
  - Anything else: `no-store` (safe dev default).
Also forces `application/wasm` (streaming compile), gzips compressible bodies for
faster first-load over Wi-Fi, binds 0.0.0.0, and prints the LAN URL.

Usage: web-serve.py [--dir build/web] [--port 8000] [--bind 0.0.0.0]
"""
import argparse, functools, gzip, http.server, io, os, re, socket, sys

# matches a content hash segment before the extension, e.g. fruit-ninja-e1312456.wasm
HASHED = re.compile(r"-[0-9a-fA-F]{6,}\.[A-Za-z0-9]+$")
GZIP_EXT = (".wasm", ".js", ".data", ".html", ".json", ".css", ".svg", ".wav")


class Handler(http.server.SimpleHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def _cache_for(self, path):
        p = path.split("?")[0]
        if p.endswith("/") or p.endswith(".html"):
            return "no-store, must-revalidate"          # index: always fresh
        if HASHED.search(p):
            return "public, max-age=31536000, immutable"  # hashed asset: forever
        return "no-store"                                 # safe dev default

    def end_headers(self):
        self.send_header("Cache-Control", self._cache_for(self.path))
        self.send_header("Access-Control-Allow-Origin", "*")
        super().end_headers()

    def guess_type(self, path):
        if str(path).endswith(".wasm"):
            return "application/wasm"
        return super().guess_type(path)

    # gzip the body when the client accepts it and the type is compressible.
    # Skip range requests (partial content must not be re-encoded).
    def send_head(self):
        if "gzip" not in self.headers.get("Accept-Encoding", "") or \
           self.headers.get("Range") or \
           not self.path.split("?")[0].endswith(GZIP_EXT):
            return super().send_head()
        path = self.translate_path(self.path)
        if not os.path.isfile(path):
            return super().send_head()
        try:
            with open(path, "rb") as f:
                raw = f.read()
        except OSError:
            return super().send_head()
        body = gzip.compress(raw, 5)
        ctype = self.guess_type(path)
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Encoding", "gzip")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Vary", "Accept-Encoding")
        self.end_headers()
        return io.BytesIO(body)


def lan_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80)); return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", default="build/web")
    ap.add_argument("--port", type=int, default=8000)
    ap.add_argument("--bind", default="0.0.0.0")
    a = ap.parse_args()
    if not os.path.isdir(a.dir):
        sys.exit("error: dir not found: %s" % a.dir)
    handler = functools.partial(Handler, directory=os.path.abspath(a.dir))
    httpd = http.server.ThreadingHTTPServer((a.bind, a.port), handler)
    ip = lan_ip()
    print("serving %s/ on http://%s:%d/  (index no-store, hashed assets immutable, gzip)"
          % (a.dir, ip, a.port))
    print("  phone -> http://%s:%d/fruit-ninja.html" % (ip, a.port))
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
