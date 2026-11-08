"""Sondas UDP locales del transporte real; se lanzan exclusivamente desde pruebas.py."""
import json
from pathlib import Path
import re
import shutil
import socket
import struct
import time
import tomllib


def registrar(escenarios):
    def entorno(p):
        return {"DBZ3_NET_AUTO": p.get("rol", "host"), "DBZ3_NET_LOCAL": "1",
                "DBZ3_NET_PORT": p.get("puerto", "25490"),
                "DBZ3_NET_JOIN_PORT": p.get("puerto", "25490")}

    def sonda(j, p):
        port = int(p.get("puerto", "25490"))
        baseline = p.get("antes") == "1"
        inicio = time.time()
        j.log(r"dbz3 rb: auto host: en marcha", 150)
        j.log(r"dbz3 online: huella de .* mods lista", 30)
        resultados = {}
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            udp.bind(("127.0.0.1", 0))
            udp.settimeout(1.2)

            def hello(build, os="windows", proto=7, echo=0, nonce=12345):
                identity = (build.encode() + b"\0" + os.encode()).ljust(40, b"\0")
                return b"DBZ3" + bytes([1, proto]) + identity + b"b" * 32 + b"\0" * 8 + struct.pack("<BIBII", 3, 0, 0, nonce, echo)

            for nombre, paquete in (("otro_build", hello("c" * 32)),
                                    ("otro_so", hello("d" * 32, "linux")),
                                    ("otro_protocolo", hello("a" * 32, proto=6))):
                udp.sendto(paquete, ("127.0.0.1", port))
                try:
                    reply, _ = udp.recvfrom(2048)
                except socket.timeout:
                    reply = b""
                resultados[nombre] = reply.hex() or "sin respuesta"
                if nombre == "otro_protocolo" and baseline:
                    assert not reply, "el protocolo viejo ya recibe respuesta; revisar medida previa"
                else:
                    assert reply[:5] == b"DBZ3\x0d", (nombre, reply)
                    assert struct.unpack_from("<I", reply, 6)[0] == 12345
                    assert reply[5] == paquete[5], "el rechazo debe poder leerlo esa version"
            if not baseline:
                # Retransmitir el mismo rechazo no debe crear otro informe ni ocupar la sala.
                udp.sendto(hello("a" * 32, proto=6), ("127.0.0.1", port))
                reply, _ = udp.recvfrom(2048)
                assert reply[:6] == b"DBZ3\x0d\x06"
                udp.sendto(hello("a" * 32, proto=6)[:-1], ("127.0.0.1", port))
                try:
                    invalid, _ = udp.recvfrom(2048)
                    raise AssertionError(f"acepto HELLO truncado: {invalid.hex()}")
                except socket.timeout:
                    pass
            # Los rechazos no deben apropiarse de la sala ni impedir una conexion correcta.
            udp.sendto(hello("a" * 32), ("127.0.0.1", port))
            reply, _ = udp.recvfrom(2048)
            assert reply[:6] == b"DBZ3\x0c\x07", reply
            nonce, challenge = struct.unpack_from("<II", reply, 6)
            assert nonce == 12345 and challenge
            udp.sendto(hello("a" * 32, echo=challenge), ("127.0.0.1", port))
            reply, _ = udp.recvfrom(2048)
            assert reply[:6] == b"DBZ3\x02\x07", reply
            resultados["compatible"] = "HELLO_ACK recibido tras los tres rechazos"
        if not baseline:
            j.log(r"dbz3 online: informe de conexion guardado", 10)
            informes = [f for f in (j.dest / "informes_online").glob("conexion_*.txt")
                        if f.stat().st_mtime >= inicio - 2 and "reason=waiting" not in f.read_text(encoding="utf-8")]
            assert len(informes) == 3, [str(f) for f in informes]
            for f in informes:
                text = f.read_text(encoding="utf-8")
                assert "protocol_local=7" in text and "rx_udp=" in text and "build_local=" in text, text
                shutil.copy2(f, j.run / f.name)
            resultados["informes"] = len(informes)
        (j.run / "sonda_online.json").write_text(json.dumps(resultados, indent=2), encoding="utf-8")
        j.info(str(resultados))

    def silencio(j, p):
        inicio = time.time()
        # Puerto abierto que recibe y no contesta: separa el timeout de un ICMP port-unreachable.
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            udp.bind(("127.0.0.1", int(p.get("puerto", "25490"))))
            j.log(r"dbz3 rb: auto guest: en marcha", 150)
            j.log(r"dbz3 online: informe de conexion guardado", 30)
        informes = [f for f in (j.dest / "informes_online").glob("conexion_*.txt") if f.stat().st_mtime >= inicio - 2]
        assert informes, "sin informe de timeout"
        text = informes[-1].read_text(encoding="utf-8")
        assert "rx_udp=0" in text and "tx_ok=0 " not in text, text
        assert "reason=timeout" in text and "rollback=0" in text, text
        shutil.copy2(informes[-1], j.run / informes[-1].name)
        j.info("timeout sin respuesta: informe con envios reales, cero recibidos y rollback=0")

    def rechazo(j, p):
        motivo = p.get("motivo", "os")
        assert motivo in ("os", "build", "protocol")
        inicio = time.time()
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            udp.bind(("127.0.0.1", int(p.get("puerto", "25490"))))
            udp.settimeout(20)
            pkt, origen = udp.recvfrom(2048)
            assert pkt[:6] == b"DBZ3\x01\x07" and len(pkt) == 100, pkt
            nonce = struct.unpack_from("<I", pkt, 92)[0]
            reply = b"DBZ3\x0d" + bytes([6 if motivo == "protocol" else 7]) + struct.pack("<I", nonce)
            if motivo != "protocol":
                reply += (b"linux" if motivo == "os" else b"windows").ljust(8, b"\0")
            udp.sendto(reply, origen)
            j.log(r"dbz3 online: informe de conexion guardado", 10)
        informes = [f for f in (j.dest / "informes_online").glob("conexion_*.txt") if f.stat().st_mtime >= inicio - 2]
        assert informes, "sin informe de rechazo"
        text = informes[-1].read_text(encoding="utf-8")
        assert f"reason={motivo}" in text and "role=guest" in text and "rollback=0" in text, text
        shutil.copy2(informes[-1], j.run / informes[-1].name)
        j.info(f"invitado: rechazo {motivo} identificado e informe guardado")

    escenarios["online_diag"] = ("Rechazos UDP SO/build/protocolo y recuperacion de la sala (sin capturas)", entorno, sonda)
    escenarios["online_silencio"] = ("Timeout UDP e informe enviable (usar --par rol=guest)", entorno, silencio)
    escenarios["online_rechazo"] = ("Rechazo del anfitrion explicado al invitado (usar --par rol=guest motivo=os|build|protocol)", entorno, rechazo)


if __name__ == "__main__":
    raiz = Path(__file__).resolve().parents[1]
    tabla = (raiz / "res/ingame_ui/i18n_modos.toml").read_text(encoding="utf-8")
    datos = tomllib.loads(tabla)
    bloque = tabla.split("# solNET 1.5.6.1:", 1)[1].split('["Low-resolution mode', 1)[0]
    textos = tomllib.loads(bloque.split("\n", 1)[1])
    codigo = "\n".join((raiz / ruta).read_text(encoding="utf-8") for ruta in ("src/net/netplay.cpp", "src/ingame/online_panel.cpp"))
    llamadas = {json.loads(en): json.loads(es) for es, en in re.findall(r'T::T\(\s*("(?:[^"\\]|\\.)*")\s*,\s*("(?:[^"\\]|\\.)*")\s*\)', codigo)}
    for clave, lenguas in textos.items():
        assert set(lenguas) == {"es", "en", "fr", "de", "it", "pt", "ja"}, clave
        assert all(lenguas.values()), clave
        assert lenguas["en"] == clave and llamadas.get(clave) == lenguas["es"], clave
    assert len(textos) == 12, len(textos)
    print(f"PASS: TOML completo ({len(datos)} claves), 12 textos nuevos x 7 idiomas y llamadas ES/EN exactas")
