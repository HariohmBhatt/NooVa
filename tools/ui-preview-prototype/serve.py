#!/usr/bin/env python3
"""Serve the throwaway NOVA Sentinel UI prototype with Python's stdlib."""

from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import os


ROOT = Path(__file__).resolve().parent
HOST = "0.0.0.0"
PORT = 4173


if __name__ == "__main__":
    os.chdir(ROOT)
    print(f"NOVA UI prototype: http://127.0.0.1:{PORT}/?variant=D")
    ThreadingHTTPServer((HOST, PORT), SimpleHTTPRequestHandler).serve_forever()
