"""Servidor falso con el formato de ntfy.sh (solo lo que usa el matchmaking) para probar sin el servicio publico.

  python tools/ntfy_fake.py 8099        (GET /<topic>/publish?message=X  y  GET /<topic>/json?poll=1&since=80s)

`Servidor(puerto)` lo arranca en un hilo (lo usa `pruebas.py online-mm`); `.mensajes` guarda todo lo publicado.
"""
import json
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse


class Servidor:
    def __init__(self, puerto=0):
        self.mensajes = []   # (tiempo, topic, texto)
        self.peticiones = []
        srv = self

        class H(BaseHTTPRequestHandler):
            def log_message(self, *a):
                pass

            def do_GET(self):
                u = urlparse(self.path)
                q = parse_qs(u.query)
                partes = u.path.strip("/").split("/")
                srv.peticiones.append(self.path)
                if len(partes) != 2:
                    self.send_response(404); self.end_headers(); return
                topic, op = partes
                cuerpo = ""
                if op == "publish":
                    srv.mensajes.append((time.time(), topic, q.get("message", [""])[0]))
                    cuerpo = json.dumps({"id": "x", "event": "message"}) + "\n"
                elif op == "json":
                    desde = q.get("since", ["80s"])[0]
                    seg = float(desde[:-1]) if desde.endswith("s") else 80.0
                    for t, tp, txt in srv.mensajes:
                        if tp == topic and time.time() - t <= seg:
                            cuerpo += json.dumps({"id": "x", "time": int(t), "event": "message", "topic": tp, "message": txt}, separators=(",", ":")) + "\n"
                b = cuerpo.encode()
                self.send_response(200)
                self.send_header("Content-Length", str(len(b)))
                self.end_headers()
                self.wfile.write(b)

        self.httpd = ThreadingHTTPServer(("127.0.0.1", puerto), H)
        self.puerto = self.httpd.server_address[1]
        threading.Thread(target=self.httpd.serve_forever, daemon=True).start()

    def cerrar(self):
        self.httpd.shutdown()


if __name__ == "__main__":
    s = Servidor(int(sys.argv[1]) if len(sys.argv) > 1 else 8099)
    print("ntfy falso en", s.puerto, flush=True)
    while True:
        time.sleep(3600)
