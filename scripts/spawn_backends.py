#!/usr/bin/env python3
import http.server
import socketserver
import threading

class FastHandler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        body = f"Upstream response from backend on port {self.server.server_address[1]}\n".encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)
        self.wfile.flush()

    def log_message(self, format, *args):
        pass

class ThreadedTCPServer(socketserver.ThreadingMixIn, socketserver.TCPServer):
    allow_reuse_address = True
    daemon_threads = True

def run_backend(port):
    server = ThreadedTCPServer(("127.0.0.1", port), FastHandler)
    server.serve_forever()

if __name__ == '__main__':
    ports = [9001, 9002, 9003]
    for p in ports:
        t = threading.Thread(target=run_backend, args=(p,), daemon=True)
        t.start()
        print(f"[+] Upstream server listening on 127.0.0.1:{p}")

    print("[*] Upstream cluster active. Press Ctrl+C to stop.")
    try:
        threading.Event().wait()
    except KeyboardInterrupt:
        print("\n[*] Stopping cluster.")
