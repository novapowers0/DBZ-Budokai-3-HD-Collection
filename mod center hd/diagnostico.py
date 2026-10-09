"""Diagnostico de un log del juego (dbz3_*.log) en lenguaje sencillo.

Los usuarios pasan su log cuando algo va mal ("va a 30", "se cierra", "los personajes
nuevos no salen"). Este modulo lee el log y devuelve una lista de hallazgos con su
explicacion y que hacer, sin jerga. Lo usa la pagina "Diagnostico" del Mod Kit y
tambien se puede usar desde la consola:

    python diagnostico.py <ruta al log> [--en]

Cada hallazgo es (nivel, titulo, detalle) con nivel "ok" | "info" | "warn" | "err".
"""

import re
import sys

ULTIMA_VERSION = (1, 4, 3, 1)   # "1.4.3 EX" (4o numero = EX)

_TS = re.compile(r"^\[(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d)")
_ENTORNO = re.compile(r"entorno os=(\S+) ram=(\d+)MB dbz3\.exe=([\d.]+) rexgpu-xenos=(\S+) rexruntime=(\S+)")
_GPU = re.compile(r"DXGI adapter: (.+?) \(vendor")
_LANG = re.compile(r"applied user settings -> .*?internal_scale=(\S+) lang=(\S+) backend=(\S+)")
_RUNTIME = re.compile(r"applied runtime settings -> internal_scale=(\S+) vsync=(\S+) msaa=(\S+) .*?hd_tex=(\S+)")
_PERF_GPU = re.compile(r"\[gpu\].*perf fps=([\d.]+) (?:.*?fg=(\d))?")
_GPU_WAIT = re.compile(r"gpu_wait=([\d.]+)ms/f")
_TIRON = re.compile(r"tiron (\d+) ms \(texload \+(\d+) gpu_wait ([\d.]+) ms cp_wait ([\d.]+) ms"
                    r"(?: shaders \+(\d+))?\)|tiron (\d+) ms \(vulkan cp_wait ([\d.]+) ms\)")
_ROSTER_OK = re.compile(r"personajes nuevos: _roster al dia \((\d+) personajes\)")
_ROSTER_CHAR = re.compile(r"dbz3 roster \[[^\]]+\]: personaje id=(\d+)")


def _v(text):
    try:
        return tuple(int(x) for x in text.split(".")[:4])
    except ValueError:
        return None


def _vs(v):
    if len(v) == 4 and v[3]:
        return "%d.%d.%d EX" % v[:3]
    return ".".join(str(x) for x in v[:3])


def analizar(texto, en=False):
    """Devuelve [(nivel, titulo, detalle)] a partir del texto de un log."""
    T = (lambda es, eng: eng) if en else (lambda es, eng: es)
    out = []
    lineas = texto.splitlines()

    # ---------------------------------------------------------------- equipo y version
    ent = None
    gpu = None
    for l in lineas:
        m = _ENTORNO.search(l)
        if m:
            ent = m
        m = _GPU.search(l)
        if m and gpu is None:
            gpu = m.group(1)
    version = None
    if ent:
        version = _v(ent.group(3))
        ram_gb = int(ent.group(2)) / 1024
        out.append(("info", T("Equipo", "System"),
                    T("Windows %s, %.0f GB de RAM, tarjeta grafica: %s.",
                      "Windows %s, %.0f GB RAM, graphics card: %s.") % (ent.group(1), ram_gb, gpu or "?")))
        if version and version < ULTIMA_VERSION:
            out.append(("warn", T("Version antigua: %s", "Old version: %s") % _vs(version),
                        T("Este log es de la version %s. La ultima es la %s: actualiza antes de seguir "
                          "buscando el fallo (muchos problemas ya estan arreglados).",
                          "This log is from version %s. The latest is %s: update first (many issues are "
                          "already fixed).") % (_vs(version), _vs(ULTIMA_VERSION))))
        elif version:
            out.append(("ok", T("Version al dia (%s)", "Up-to-date version (%s)") % _vs(version), ""))
        gpu_v, rt_v = _v(ent.group(4)), _v(ent.group(5))
        if version and ((gpu_v and gpu_v != version) or (rt_v and rt_v != version)):
            out.append(("err", T("Instalacion mezclada", "Mixed installation"),
                        T("dbz3.exe es %s pero las DLL del motor son %s / %s: se copio una version encima de "
                          "otra a medias. Descomprime la release completa en una carpeta limpia o usa "
                          "\"Reparar instalacion\" en el launcher.",
                          "dbz3.exe is %s but the engine DLLs are %s / %s: one version was half-copied over "
                          "another. Unzip the full release into a clean folder or use \"Repair "
                          "installation\" in the launcher.") % (_vs(version), ent.group(4), ent.group(5))))
    elif gpu:
        out.append(("info", T("Tarjeta grafica", "Graphics card"), gpu))

    # ---------------------------------------------------------------- region e idioma
    eu = any("detected EU/PAL" in l or "(region eu)" in l for l in lineas)
    lang = None
    escala = None
    for l in lineas:
        m = _LANG.search(l)
        if m:
            escala, lang = m.group(1), m.group(2)
    if lang:
        out.append(("info", T("Juego", "Game"),
                    T("Version %s, idioma %s.", "%s version, language %s.") % ("EU/PAL" if eu else "US/NA", lang)))

    # ---------------------------------------------------------------- personajes nuevos
    roster = [l for l in lineas if _ROSTER_CHAR.search(l)]
    roster_build = [m for m in (_ROSTER_OK.search(l) for l in lineas) if m]
    no_us = any("imagen no US" in l for l in lineas)
    if no_us and (roster_build or any("_roster" in l for l in lineas)):
        out.append(("warn", T("Personajes nuevos con la version EU", "New characters with the EU version"),
                    T("Los personajes nuevos solo funcionan con el juego US/NA. Con el ejecutable europeo el "
                      "juego arranca normal pero sin ellos. Usa el default.xex US/NA para jugarlos.",
                      "New characters only work with the US/NA game. With the European executable the game "
                      "runs normally but without them. Use the US/NA default.xex to play them.")))
    elif roster:
        out.append(("ok", T("Personajes nuevos cargados: %d", "New characters loaded: %d") % len(roster), ""))

    # ---------------------------------------------------------------- rendimiento
    perf = []
    espera_lenta = []     # gpu_wait (ms/frame) de las ventanas a 30 FPS (v1.4.1+)
    for l in lineas:
        m = _PERF_GPU.search(l)
        if m:
            perf.append((float(m.group(1)), m.group(2) != "0"))   # Vulkan no trae fg=: cuenta como en foco
            w = _GPU_WAIT.search(l)
            if w and float(m.group(1)) < 50:
                espera_lenta.append(float(w.group(1)))
    if perf:
        fg = [f for f, focused in perf if focused]
        bg = [f for f, focused in perf if not focused]
        racha = mejor = 0
        for f, _ in perf:
            racha = racha + 1 if 27.0 <= f <= 33.0 else 0
            mejor = max(mejor, racha)
        if mejor >= 3:
            if version and version < (1, 4, 1):
                det = T("El juego se quedo clavado a 30 FPS. En la version %s esto pasa incluso en equipos muy "
                        "potentes por como Windows 11 mide las pausas cortas del juego. La 1.4.1 trae "
                        "arreglos para esto: actualiza y, si sigue, pasa el registro nuevo (dira la causa "
                        "exacta).",
                        "The game got stuck at 30 FPS. In version %s this happens even on very powerful PCs "
                        "because of how Windows 11 times the game's short pauses. 1.4.1 includes fixes for this: "
                        "update and, if it continues, share the new log (it names the exact cause).") % \
                    _vs(version)
            elif espera_lenta and sum(espera_lenta) / len(espera_lenta) < 2.0:
                det = T("El juego se quedo clavado a 30 FPS y la tarjeta grafica estaba casi parada. Lo mas "
                        "comun: un limite de FPS externo a 60 (panel de NVIDIA/AMD 'Max Frame Rate' o "
                        "RivaTuner) deja el juego a 30; quitalo y usa el limitador del launcher. Si no hay "
                        "ninguno, el freno es el procesador: cierra programas en segundo plano, pon el plan "
                        "de energia de Windows en alto rendimiento y enchufa el portatil.",
                        "The game got stuck at 30 FPS while the graphics card was nearly idle. Most common "
                        "cause: an external 60 FPS limit (NVIDIA/AMD control panel 'Max Frame Rate' or "
                        "RivaTuner) locks the game to 30; remove it and use the launcher's limiter. If there is "
                        "none, the processor is the bottleneck: close background apps, set the Windows power "
                        "plan to high performance and plug in a laptop.")
            else:
                det = T("El juego se quedo clavado a 30 FPS: cada fotograma tarda un poco mas de lo que da la "
                        "pantalla. Prueba a bajar la escala interna a 1x, quitar el MSAA o la mejora de "
                        "texturas, y cierra programas que graben o pongan capas encima del juego.",
                        "The game got stuck at 30 FPS: each frame takes slightly longer than the screen "
                        "allows. Try internal scale 1x, MSAA off or texture enhancement off, and close apps "
                        "that record or draw overlays on top of the game.")
            out.append(("err", T("Va a 30 FPS", "Running at 30 FPS"), det))
        elif fg and min(fg) < 50:
            out.append(("warn", T("Bajones de FPS", "FPS drops"),
                        T("El peor momento bajo a %.0f FPS (media %.0f).", "Worst moment dropped to %.0f FPS "
                          "(average %.0f).") % (min(fg), sum(fg) / len(fg))))
        elif fg:
            out.append(("ok", T("Rendimiento correcto (%.0f FPS de media)", "Performance OK (%.0f FPS average)")
                        % (sum(fg) / len(fg)), ""))
        if bg and fg and sum(bg) / len(bg) < 50 <= sum(fg) / len(fg):
            out.append(("info", T("Mas lento en segundo plano", "Slower in the background"),
                        T("Con la ventana sin foco va mas lento. Es normal si estabas en otra ventana.",
                          "It runs slower while the window is unfocused. Normal if you were in another window.")))
    tirones = [m for m in (_TIRON.search(l) for l in lineas) if m]
    if tirones:
        peor = max(int(m.group(1) or m.group(6)) for m in tirones)
        tex = sum(int(m.group(2) or 0) for m in tirones) / len(tirones)
        gw = sum(float(m.group(3) or 0) for m in tirones) / len(tirones)
        sh = sum(int(m.group(5) or 0) for m in tirones)
        if sh >= len(tirones):
            causa = T("efectos que aparecen por primera vez (se preparan sus sombreadores); la siguiente "
                      "vez ya no pasa. Si te molesta, activa \"Compilar shaders en segundo plano\" "
                      "(pestana Dev del launcher)",
                      "effects shown for the first time (their shaders are being prepared); it does not "
                      "happen the next time. If it bothers you, enable \"Compile shaders in the "
                      "background\" (launcher Dev tab)")
        elif tex >= 40:
            causa = T("sobre todo carga de texturas", "mostly texture loading")
        elif gw >= 20:
            causa = T("la tarjeta grafica", "the graphics card")
        else:
            causa = T("el propio juego (lectura de disco o procesador)", "the game itself (disk read or CPU)")
        out.append(("info" if len(tirones) < 5 else "warn",
                    T("%d tirones (el peor de %d ms)", "%d hitches (worst %d ms)") % (len(tirones), peor),
                    T("Momentos en que un fotograma tardo mas de 50 ms. Causa probable: %s.",
                      "Moments where a frame took longer than 50 ms. Likely cause: %s.") % causa))
    if escala and escala not in ("1x", "1"):
        out.append(("info", T("Escala interna %s", "Internal scale %s") % escala,
                    T("Una escala mayor que 1x es lo que mas cuesta a la grafica. Si va lento, pon 1x.",
                      "A scale above 1x is the most expensive setting. If it is slow, use 1x.")))

    # ---------------------------------------------------------------- avisos y errores
    for l in lineas:
        if "lecturas de disco lentas" in l:
            out.append(("warn", T("Disco lento", "Slow disk"),
                        T("Algunas lecturas del disco tardaron mucho. Si el juego esta en un disco duro "
                          "mecanico o en una unidad USB, muevelo a un SSD.",
                          "Some disk reads were very slow. If the game is on a mechanical hard drive or a USB "
                          "drive, move it to an SSD.")))
            break
    # "DRED (Device Removed Extended Data) enabled" sale en todos los logs: no es un fallo.
    if any("D3D12 device removed:" in l or "DXGI_ERROR_DEVICE_REMOVED" in l for l in lineas):
        out.append(("err", T("La tarjeta grafica se reinicio", "The graphics card reset"),
                    T("El controlador de la grafica fallo (device removed). Actualiza el driver de la grafica "
                      "y quita overclocks; si sigue, prueba el modo Vulkan en el launcher.",
                      "The graphics driver failed (device removed). Update the graphics driver and remove "
                      "overclocks; if it continues, try Vulkan mode in the launcher.")))
    errores = [l for l in lineas if "[error]" in l or "[critical]" in l]
    if errores:
        ej = errores[0].split("] ", 4)[-1][:160]
        out.append(("warn", T("%d lineas de error", "%d error lines") % len(errores),
                    T("Primera: %s", "First: %s") % ej))
    if any("lr=0x82134A98" in l for l in lineas):
        out.append(("err", T("Cierre al cambiar de traje en un personaje nuevo",
                              "Crash when changing costume on a new character"),
                    T("Es un fallo conocido de la 1.4.0 (sobre todo con Janemba): el juego pedia un traje "
                      "que el personaje nuevo no tiene. Esta corregido en la 1.4.1.",
                      "A known 1.4.0 bug (mostly Janemba): the game asked for a costume the new character "
                      "does not have. Fixed in 1.4.1.")))
    if any("roster_build.py" in l and "can't open file" in l for l in lineas):
        out.append(("info", T("Personajes nuevos sin el kit de modding", "New characters without the modding kit"),
                    T("Tienes el pack de personajes pero no el kit: es normal y los personajes funcionan. "
                      "Solo hace falta el kit para crear o cambiar personajes.",
                      "You have the character pack but not the kit: that is fine and the characters work. "
                      "The kit is only needed to create or change characters.")))
    arranco = any("OnPostLaunchModule - guest thread created" in l for l in lineas)
    cerrado = any("Window close requested" in l or "exiting dbz3" in l for l in lineas)
    if arranco and not cerrado and lineas:
        ult = next((m.group(1) for m in (_TS.match(l) for l in reversed(lineas)) if m), "?")
        out.append(("err", T("El juego se cerro de golpe", "The game closed suddenly"),
                    T("El log se corta en %s sin un cierre normal. Si fue un cierre inesperado, pasa este "
                      "log y explica que estabas haciendo en ese momento.",
                      "The log stops at %s without a normal exit. If it was a crash, share this log and say "
                      "what you were doing at that moment.") % ult))
    if not out:
        out.append(("warn", T("No parece un log del juego", "This does not look like a game log"),
                    T("Abre un archivo dbz3_XXX.log de la carpeta logs del juego.",
                      "Open a dbz3_XXX.log file from the game's logs folder.")))
    return out


def informe(texto, en=False):
    marca = {"ok": "[OK]", "info": "[i]", "warn": "[!]", "err": "[X]"}
    filas = []
    for nivel, titulo, detalle in analizar(texto, en):
        filas.append("%s %s" % (marca[nivel], titulo))
        if detalle:
            filas.append("    " + detalle)
    return "\n".join(filas)


def main(argv):
    if len(argv) < 2:
        print("uso: python diagnostico.py <dbz3_XXX.log> [--en]")
        return 2
    with open(argv[1], encoding="utf-8", errors="replace") as f:
        texto = f.read()
    print(informe(texto, "--en" in argv))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
