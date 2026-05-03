#!/usr/bin/env python3
"""
Local development server for the Nutrimatic web interface (Russian index).
Serves web_static/ and runs cgi-search.py for the root URL.

Usage (from project root):
  python3 run_server.py [port]
  # Default port 8765. Open http://localhost:8765/

Requires: build/find-expr, wiki-merged.index, cgi_scripts/cgi-search.py

If searches fail with "can't parse" for S, G, or other operators shown on the
home page, rebuild find-expr from this tree (e.g. ``conan build .`` or your
Meson build) so the binary matches ``source/expr-parse.cpp``.
"""

import os
import subprocess
import sys
from pathlib import Path

from http.server import HTTPServer, BaseHTTPRequestHandler

def strip_cgi_headers(stdout: bytes) -> bytes:
    """Drop CGI-style stdout headers so they are not sent in the HTTP body."""
    if b"\r\n\r\n" in stdout:
        return stdout.split(b"\r\n\r\n", 1)[1]
    if b"\n\n" in stdout:
        return stdout.split(b"\n\n", 1)[1]
    return stdout


TOP = Path(__file__).resolve().parent
WEB_STATIC = TOP / "web_static"
CGI_SCRIPT = TOP / "cgi_scripts" / "cgi-search.py"
FIND_EXPR = TOP / "build" / "find-expr"
INDEX = TOP / "wiki-merged.index"


class NutrimaticHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        # Strip query string for path; keep for CGI
        path = self.path.split("?")[0]
        query_string = self.path.partition("?")[2] if "?" in self.path else ""

        if path == "/" or path == "":
            self._run_cgi(query_string)
            return

        # Serve static files from web_static
        path = path.lstrip("/")
        if ".." in path or path.startswith("/"):
            self.send_error(404)
            return
        filepath = WEB_STATIC / path
        if not filepath.is_file():
            # Try index for directory
            if (WEB_STATIC / path).is_dir():
                filepath = WEB_STATIC / path / "index.html"
            if not filepath.is_file():
                self.send_error(404)
                return
        self._serve_file(filepath)

    def _run_cgi(self, query_string):
        if not FIND_EXPR.is_file():
            self.send_error(500, "find-expr not found (run 'conan build .')")
            return
        if not INDEX.is_file():
            self.send_error(500, "wiki-merged.index not found (run build_ruwiki_index.sh)")
            return
        if not CGI_SCRIPT.is_file():
            self.send_error(500, "cgi-search.py not found")
            return

        env = os.environ.copy()
        env["REQUEST_METHOD"] = "GET"
        env["QUERY_STRING"] = query_string
        env["NUTRIMATIC_FIND_EXPR"] = str(FIND_EXPR)
        env["NUTRIMATIC_INDEX"] = str(INDEX)

        try:
            r = subprocess.run(
                [sys.executable, str(CGI_SCRIPT)],
                env=env,
                cwd=str(TOP),
                capture_output=True,
                timeout=120,
            )
        except subprocess.TimeoutExpired:
            self.send_error(504, "Search timed out")
            return
        except Exception as e:
            self.send_error(500, str(e))
            return

        body = strip_cgi_headers(r.stdout)

        self.send_response(200)
        self.send_header("Content-type", "text/html; charset=utf-8")
        self.end_headers()
        self.wfile.write(body)

        if r.stderr:
            self.log_error("%s", r.stderr.decode(errors="replace"))

    def _serve_file(self, filepath):
        suffix = filepath.suffix.lower()
        content_types = {
            ".html": "text/html; charset=utf-8",
            ".css": "text/css",
            ".js": "application/javascript",
            ".ico": "image/x-icon",
            ".txt": "text/plain",
        }
        ct = content_types.get(suffix, "application/octet-stream")
        try:
            data = filepath.read_bytes()
        except OSError:
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header("Content-type", ct)
        self.send_header("Content-length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, format, *args):
        sys.stderr.write("%s - [%s] %s\n" % (self.address_string(), self.log_date_time_string(), format % args))


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
    server = HTTPServer(("", port), NutrimaticHandler)
    print("Nutrimatic (Russian index) at http://localhost:%d/" % port)
    print("Ctrl+C to stop.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        server.shutdown()


if __name__ == "__main__":
    main()
