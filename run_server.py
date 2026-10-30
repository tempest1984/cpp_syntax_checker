#!/usr/bin/env python3
"""
Launcher for C++ Parser Web Application.
Automatically detects a free port, opens the default browser, and launches the server.
"""

import sys
import socket
import webbrowser
import threading
import time
from server import app, PARSER_BIN

def find_free_port(start_port=5000, max_port=5050):
    for port in range(start_port, max_port):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            try:
                s.bind(('127.0.0.1', port))
                return port
            except OSError:
                continue
    return start_port

def open_browser(url):
    time.sleep(1.2)
    try:
        webbrowser.open(url)
    except Exception:
        pass

if __name__ == '__main__':
    port = find_free_port(5000)
    url = f"http://127.0.0.1:{port}"
    
    print("=" * 65)
    print("           C++ SYNTAX PARSER & ANALYZER WEB APP          ")
    print("=" * 65)
    print(f" [*] Parser binary:  {PARSER_BIN}")
    print(f" [*] Local web URL:  {url}")
    print(" [*] Code Editor:     Monaco (VS Code) + Fallback")
    print(" [*] File Uploader:   Active (.cpp, .c, .h, .hpp, .txt)")
    print(" [*] Press Ctrl+C to stop the server anytime.")
    print("=" * 65)
    
    # Open browser automatically in background
    if "--no-browser" not in sys.argv:
        threading.Thread(target=open_browser, args=(url,), daemon=True).start()
    
    try:
        app.run(host='127.0.0.1', port=port, debug=False)
    except KeyboardInterrupt:
        print("\n[*] Server stopped.")

