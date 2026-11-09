#!/usr/bin/env bash
# Local dev server for the web UI, reachable from other machines on the LAN.
#
#   http://<host>:8000/      mock mode works; Web Serial/WebUSB need HTTPS
#   https://<host>:8443/     self-signed cert (accept the warning once); Connect and Flash work
#
# Serves web/ at / and firmware/build/ at /firmware/ so the Firmware panel can
# list and flash the latest build. Ctrl-C to stop.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

CERT_DIR=tools/.certs
mkdir -p "$CERT_DIR"
if [ ! -f "$CERT_DIR/cert.pem" ]; then
    echo ">> Generating self-signed certificate in $CERT_DIR"
    openssl req -x509 -newkey rsa:2048 -nodes -keyout "$CERT_DIR/key.pem" -out "$CERT_DIR/cert.pem" \
        -days 365 -subj "/CN=elrs-sim-rx-dev" >/dev/null 2>&1
fi

HTTP_PORT="${HTTP_PORT:-8000}"
HTTPS_PORT="${HTTPS_PORT:-8443}"
IP="$(hostname -I 2>/dev/null | awk '{print $1}')"
echo ">> http://${IP:-localhost}:${HTTP_PORT}/?mock   (mock only)"
echo ">> https://${IP:-localhost}:${HTTPS_PORT}/      (device access)"

exec python3 - "$HTTP_PORT" "$HTTPS_PORT" "$CERT_DIR" <<'EOF'
import http.server, ssl, threading, sys, os, posixpath, urllib.parse

http_port, https_port, cert_dir = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3]
WEB = os.path.abspath("web")
FW = os.path.abspath("firmware/build")

class Handler(http.server.SimpleHTTPRequestHandler):
    def translate_path(self, path):
        path = urllib.parse.urlparse(path).path
        path = posixpath.normpath(urllib.parse.unquote(path))
        if path.startswith("/firmware/"):
            rel = path[len("/firmware/"):]
            return os.path.join(FW, *[p for p in rel.split("/") if p not in ("", ".", "..")])
        return os.path.join(WEB, *[p for p in path.split("/") if p not in ("", ".", "..")])
    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()
    def log_message(self, fmt, *args):
        if "/channels" not in (args[0] if args else ""):
            sys.stderr.write("%s - %s\n" % (self.address_string(), fmt % args))

def serve(port, tls):
    srv = http.server.ThreadingHTTPServer(("0.0.0.0", port), Handler)
    if tls:
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.load_cert_chain(os.path.join(cert_dir, "cert.pem"), os.path.join(cert_dir, "key.pem"))
        srv.socket = ctx.wrap_socket(srv.socket, server_side=True)
    srv.serve_forever()

threading.Thread(target=serve, args=(https_port, True), daemon=True).start()
try:
    serve(http_port, False)
except KeyboardInterrupt:
    pass
EOF
