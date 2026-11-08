"""Pulido visual 1.5.6.1. Diálogos sin aplicar partidas y rechazos UDP locales."""
import ctypes
import hashlib
import json
import re
import time


def registrar(g):
    def entorno(p):
        idioma = p.get("idioma", "5")
        g["os"].environ["DBZ3_TEST_CFG"] = (
            'dbz3_skip_launcher=false;dbz3_first_steps_seen=true;'
            'dbz3_news_seen="1.5.6.1";dbz3_language=' + idioma +
            ';dbz3_ui_language=' + idioma)
        return {"DBZ3_TEST_SAVE100_UI": "1"}

    def preparar(j, p, env):
        p["sin_arranque"] = "1"
        request = j.dest / "dbz3_save100_input.req"
        if request.exists():
            g["shutil"].move(str(request), str(j.run / "input_pendiente_anterior.req"))

    def fotos(j, p, accion):
        h = g["_hwnd_juego"](j.proc.pid)
        assert h, "sin ventana propia"
        u, rect = g["U32"], g["_RECT"]

        def foto(nombre):
            path = j.captura(nombre)
            with g["Image"].open(path) as im:
                im.resize((im.width // 2, im.height // 2)).save(path)

        resultados = []
        tamanos = ((1280, 720),) if p.get("antes") else ((1280, 720), (1600, 900))
        for w, hh in tamanos:
            rw, rc = rect(), rect()
            u.GetWindowRect(h, ctypes.byref(rw))
            u.GetClientRect(h, ctypes.byref(rc))
            u.SetWindowPos(h, None, rw.left, rw.top,
                           w + rw.right - rw.left - rc.right,
                           hh + rw.bottom - rw.top - rc.bottom, 0x0004 | 0x0010)
            time.sleep(0.7)
            u.GetClientRect(h, ctypes.byref(rc))
            assert (rc.right, rc.bottom) == (w, hh), (rc.right, rc.bottom)
            accion(f"{p.get('idioma', '5')}_{w}x{hh}", foto)
            resultados.append({"idioma": p.get("idioma", "5"), "client": [rc.right, rc.bottom]})
        return resultados

    def prueba(j, p):
        j.log(r"dbz3: launcher shown", 90)
        time.sleep(1)
        j.tecla_real("Escape")
        def hashes():
            return {str(f.relative_to(j.dest)): hashlib.sha256(f.read_bytes()).hexdigest()
                    for f in (j.dest / "user_data").rglob("*") if f.is_file()
                    and f.suffix in (".bin", ".header")}

        antes = hashes()
        def dialogos(pref, foto):
            foto(pref + "_launcher")
            g["_save100_click"](j, "open")
            foto(pref + "_desbloquear")
            g["_save100_click"](j, "restore")
            foto(pref + "_restaurar")
            # Cancelar real, a la derecha del botón medido. ConfirmModal usa
            # 220/120 px y ItemSpacing.x=10: distancia entre centros=180.
            # Escape por PostMessage no llega a este popup en el arnés.
            rx = r"save_100 widget confirm_restore at (\d+),(\d+)"
            x, y = map(int, re.findall(rx, j.texto_log())[-1])
            request = j.dest / "dbz3_save100_input.req"
            assert not request.exists(), "clic de restaurar pendiente"
            request.write_text(f"{x + 180} {y}", encoding="ascii")
            time.sleep(0.5)
            g["_save100_click"](j, "close")
        resultados = fotos(j, p, dialogos)
        assert antes == hashes(), "abrir/cancelar modificó una partida"
        (j.run / "solui.json").write_text(json.dumps(resultados), encoding="utf-8")
        j.info("diálogos abiertos/cancelados, capturas 0,5 y partidas intactas")

    g["ESCENARIOS"]["solui"] = ("solUI: launcher y diálogos a 720p/900p, sin cambiar partidas", entorno, prueba, preparar)

    def entorno_online(p):
        entorno(p)
        g["os"].environ["DBZ3_TEST_CFG"] = g["os"].environ["DBZ3_TEST_CFG"].replace(
            "dbz3_skip_launcher=false", "dbz3_skip_launcher=true")
        return {"DBZ3_NET_AUTO": "guest", "DBZ3_NET_LOCAL": "1",
                "DBZ3_NET_PORT": "25493", "DBZ3_NET_JOIN_PORT": "25493"}

    def online(j, p):
        # Reutiliza la sonda real del frente online; no inyecta textos en el juego.
        p["puerto"] = "25493"
        motivo = p.get("motivo", "os")
        escenario = "online_silencio" if motivo == "timeout" else "online_rechazo"
        g["ESCENARIOS"][escenario][2](j, p)
        j.log(r"dbz3 online: se abre la pantalla ONLINE", 15)
        resultados = fotos(j, p, lambda pref, foto: foto(pref + "_online_" + motivo))
        (j.run / "solui_online.json").write_text(json.dumps(resultados), encoding="utf-8")
        j.info("aviso online real capturado a 720p/900p")

    g["ESCENARIOS"]["solui_online"] = ("solUI: aviso online real, UDP local y capturas 0,5", entorno_online, online)
