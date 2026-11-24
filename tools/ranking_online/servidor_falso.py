"""Servidor local falso del ranking online (RANK-ONLINE), solo para pruebas: hace de ntfy y de raw.githubusercontent a la vez.

  python tools/ranking_online/servidor_falso.py [puerto=8097] [--fallar N] [--sin-relleno]

  GET /<tema>/publish?message=X        como ntfy (las N primeras contestan 503: prueba la cola sin conexion)
  GET /<tema>/json?poll=1&since=12h    como ntfy
  GET /datos/<ruta>                    pasa validar.py (como la Action) sobre lo recibido y sirve el JSON que sale
Rellena las tablas con jugadores inventados (los de las maquetas). La clave: RANK_HMAC_KEY o la de clave.py.
El juego apunta aqui con dbz3_rank_url=http://127.0.0.1:8097 y dbz3_rank_datos=http://127.0.0.1:8097/datos.
"""
import json
import os
import sys
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, parse_qsl, urlparse

sys.path.insert(0, str(Path(__file__).parent))
import validar  # noqa: E402

CLAVE_TXT = Path(r"E:\DBZ3HD_trabajo\ranking_online\clave\RANK_HMAC_KEY.txt")
SURV = [("Kakarotto_77", 0, 1800, 4210, 1), ("LaMujerDe17", 7, 1800, 3984, 1), ("PrincipeSaiyan", 0, 1800, 3711, 1),
        ("ZenoSama", 7, 1721, 3302, 0), ("Yamcha_Vive", 0, 1635, 3120, 0), ("Kame_Senin", 7, 1562, 2988, 0), ("NamekStar", 0, 1530, 2810, 0)]
ROGUE = [("NamekStar", 9, 1, 4800, 140), ("Kakarotto_77", 8, 1, 4040, 136), ("ZenoSama", 8, 1, 3950, 140), ("Kame_Senin", 7, 1, 3700, 130),
         ("LaMujerDe17", 6, 1, 3880, 100), ("Yamcha_Vive", 6, 1, 3720, 130), ("PrincipeSaiyan", 5, 1, 4400, 150)]


class Falso:
    def __init__(self, puerto, fallar=0, relleno=True):
        self.clave = os.environ.get("RANK_HMAC_KEY") or (CLAVE_TXT.read_text(encoding="utf-8").strip() if CLAVE_TXT.is_file() else "dbz3-rank-dev")
        self.repo = Path(tempfile.mkdtemp(prefix="rank_falso_", dir=r"D:\tmp" if Path(r"D:\tmp").is_dir() else None))
        self.mensajes, self.nuevos, self.fallar, self.relleno = [], [], fallar, relleno
        self.dias_rellenos = set()
        self.mu = threading.Lock()
        if relleno:
            ahora = int(time.time())
            for i, (al, j, sg, k, w) in enumerate(SURV):
                self.nuevos.append((ahora - 3600 + i, validar.armar(self.clave, v=1, m="s", id=f"{0xf00 + i:016x}", u=f"{0xa00 + i:016x}", al=al, ver="1.5.7",
                                                                   t=ahora - 3600, j=j, sg=sg, p=validar.puntos_surv(sg, k, w), k=k, n=60, w=w, e="llanura", df=0)))
        srv = self

        class H(BaseHTTPRequestHandler):
            def log_message(self, *a):
                pass

            def do_GET(self):
                u = urlparse(self.path)
                partes = u.path.strip("/").split("/")
                print(time.strftime("%H:%M:%S"), "GET", u.path, flush=True)
                if partes[0] == "datos":
                    srv.procesar()
                    f = srv.repo.joinpath(*partes[1:])
                    return self.responder(200, f.read_bytes()) if f.is_file() else self.responder(404, b"404: Not Found")
                if len(partes) != 2:
                    return self.responder(404, b"")
                tema, op = partes
                if op == "publish":
                    with srv.mu:
                        if srv.fallar > 0:
                            srv.fallar -= 1
                            print("   (falla a proposito: 503)", flush=True)
                            return self.responder(503, b"")
                        msg = parse_qs(u.query).get("message", [""])[0]
                        srv.mensajes.append((int(time.time()), tema, msg))
                        srv.nuevos.append((int(time.time()), msg))
                        srv.rellenar_rogue(msg)
                    return self.responder(200, json.dumps({"id": "x", "event": "message"}).encode() + b"\n")
                if op == "json":
                    with srv.mu:
                        cuerpo = "".join(json.dumps({"id": "x", "time": t, "event": "message", "topic": tp, "message": m}) + "\n"
                                         for t, tp, m in srv.mensajes if tp == tema)
                    return self.responder(200, cuerpo.encode())
                return self.responder(404, b"")

            def responder(self, codigo, cuerpo):
                self.send_response(codigo)
                self.send_header("Content-Length", str(len(cuerpo)))
                self.end_headers()
                self.wfile.write(cuerpo)

        self.httpd = ThreadingHTTPServer(("127.0.0.1", puerto), H)
        self.puerto = self.httpd.server_address[1]
        threading.Thread(target=self.httpd.serve_forever, daemon=True).start()

    def rellenar_rogue(self, msg):   # con mu: el dia y el luchador de la partida que llega (el del dia, para todos)
        if not self.relleno:
            return
        q = dict(parse_qsl(msg.rpartition("&h=")[0]))
        if q.get("m") != "r" or q.get("d") in self.dias_rellenos:
            return
        self.dias_rellenos.add(q["d"])
        d, j, ahora = int(q["d"]), int(q["j"]), int(time.time())
        for i, (al, g, b, vi, sg) in enumerate(ROGUE):
            self.nuevos.insert(0, (ahora - 600 + i, validar.armar(self.clave, v=1, m="r", id=f"{0xe00 + d * 16 + i:016x}", u=f"{0xa00 + i:016x}", al=al, ver="1.5.7",
                                                                  t=ahora - 600, j=j, sg=sg, p=validar.puntos_rogue(g, b, vi, sg), d=d,
                                                                  se=validar.semilla_diaria(d), g=g, b=b, vi=vi, c="ki_01,fisica_03,ki_01")))

    def procesar(self):
        with self.mu:
            nuevos, self.nuevos = self.nuevos, []
        if nuevos or not (self.repo / "datos" / "entradas.jsonl").is_file():
            res = validar.procesar(nuevos, self.repo, [self.clave])
            if res:
                print("   validar:", res, flush=True)


if __name__ == "__main__":
    a = sys.argv[1:]
    puerto = int(a[0]) if a and a[0].isdigit() else 8097
    fallar = int(a[a.index("--fallar") + 1]) if "--fallar" in a else 0
    f = Falso(puerto, fallar, "--sin-relleno" not in a)
    print(f"ranking falso en 127.0.0.1:{f.puerto} (repo {f.repo}, falla {fallar})", flush=True)
    while True:
        time.sleep(3600)
