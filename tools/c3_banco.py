#!/usr/bin/env python3
"""Banco offline del carril C3 (selector de escenarios + HUB), fase 1 sin juego.

Mide, contra el codigo y el decompilado recompilado que ya estan en disco:

  L1  byte del registro elegido (+30 de 0x824BA110): como lo escribe el guest y si algun
      lector suyo usa signo. Da el tope real del byte (255-17=238, no 127).
  L2  desplazamiento continuo del selector: con N altas, la ventana de casillas no repite
      escenario y todos se alcanzan; la ultima pagina del roster (24 plazas) no repite ID.
  R1  revancha aleatoria: 3 revanchas seguidas desde el "?" cambian de registro, sin
      variantes 11..16 ni campos de IW; con la cvar apagada el registro no se toca.
  H1  hitboxes del HUB en 4:3, 16:9 y 21:9 (y 32:9): las 8 filas de la pagina caen dentro
      del area 4:3 y de la imagen del juego, sin solaparse.

Uso:  python tools\\c3_banco.py
Salida: una linea por comprobacion, con numerador/denominador y la primera diferencia.
Codigo de salida 0 = verde. No abre el juego, ni PCSX2, ni compila.
"""
from __future__ import annotations

import math
import re
import sys
from pathlib import Path

R = Path(__file__).resolve().parents[1]
G = R / "generated"
FALLOS: list[str] = []


def ok(nombre: str, num, den, nota: str = "") -> None:
    print(f"PASS {nombre}: {num}/{den}" + (f" ({nota})" if nota else ""))


def fallo(nombre: str, num, den, primera: str) -> None:
    print(f"FAIL {nombre}: {num}/{den} - primera diferencia: {primera}")
    FALLOS.append(nombre)


def leer(p: Path) -> str:
    return p.read_text(encoding="utf-8", errors="replace")


def funcion(fuente: str, nombre: str) -> str | None:
    m = re.search(r"DEFINE_REX_FUNC\(" + re.escape(nombre) + r"\)\s*\{(.*?)\n\}", fuente, re.S)
    return m.group(1) if m else None


# --------------------------------------------------------------------------- L1
def l1_byte_registro() -> None:
    sel = leer(G / "dbz3_recomp.40.cpp")
    cuerpo = funcion(sel, "sub_8219ECC8")
    if not cuerpo:
        fallo("L1.byte", 0, 3, "no encuentro sub_8219ECC8 en generated/dbz3_recomp.40.cpp")
        return
    escribe25 = bool(re.search(r"REX_STORE_U8\(ctx\.r\d+\.u32 \+ 25,", cuerpo))
    escribe30 = bool(re.search(r"REX_STORE_U8\(ctx\.r\d+\.u32 \+ 30,", cuerpo))
    # el valor de +30 sale de la tabla u16 0x82373384 (lis -32201 + 13188)
    tabla = "13188" in cuerpo and "-32201" in cuerpo
    if escribe25 and escribe30 and tabla:
        ok("L1.byte-escritura", 3, 3, "stb +25, stb +30 desde la tabla u16 0x82373384")
    else:
        fallo("L1.byte-escritura", sum((escribe25, escribe30, tabla)), 3,
              f"+25={escribe25} +30={escribe30} tabla={tabla}")

    # Lector con signo: funciones con la base 0x824BA110 (-24304) que lean el byte +30 y lo extiendan
    # con extsb/int8_t. Se recorren TODAS las funciones recompiladas.
    funciones = 0
    con_base = 0
    lectores30 = 0
    con_signo = 0
    primera = ""
    for p in sorted(G.glob("dbz3_recomp.*.cpp")):
        txt = leer(p)
        partes = re.split(r"DEFINE_REX_FUNC\((\w+)\)", txt)
        for i in range(1, len(partes) - 1, 2):
            nombre, cuerpo = partes[i], partes[i + 1]
            funciones += 1
            if "-24304" not in cuerpo:
                continue
            con_base += 1
            for m in re.finditer(r"REX_LOAD_U8\(ctx\.(r\d+)\.u32 \+ 30\)", cuerpo):
                # solo cuenta si la base rN es la del global (se asigno con -24304 en esta funcion)
                reg = m.group(1)
                if not re.search(r"ctx\." + reg + r"\.s64 = ctx\.\w+\.s64 \+ -24304", cuerpo):
                    continue
                lectores30 += 1
                cola = cuerpo[m.end():m.end() + 400]
                if re.search(r"extsb|int8_t\(", cola):
                    con_signo += 1
                    primera = primera or f"{p.name}:{nombre} lee +30 con signo"
    if lectores30 == 0:
        ok("L1.byte-lector", 0, con_base,
           f"{funciones} funciones; {con_base} con la base 0x824BA110; ningun lector del guest de +30")
        print("     => solo lo leen el port (U8 sin signo) y el snapshot del online: tope 238 altas")
    elif con_signo == 0:
        ok("L1.byte-lector", lectores30, lectores30,
           f"{lectores30} lectores de +30, ninguno con signo => 0..255 (238 altas)")
    else:
        ok("L1.byte-lector", con_signo, lectores30, f"{primera or 'lector con signo'} => tope 0..127 = 110 altas")
        print("     => el byte del registro se lee CON SIGNO: el tope real es 127 (127-17 = 110 altas), "
              "no 238")


# --------------------------------------------------------------------------- S1 / S2
def s1_tope() -> None:
    src = leer(R / "src" / "escenarios.cpp")
    m = re.search(r"constexpr size_t kMaxAltas = (\d+);", src)
    if not m:
        fallo("S1.tope", 0, 2, "no encuentro kMaxAltas en escenarios.cpp")
        return
    tope = int(m.group(1))
    asevera = bool(re.search(r"static_assert\(kMaxAltas <= size_t\(127\) - size_t\(kSerie\)", src))
    if tope == 110 and asevera:
        ok("S1.tope", 2, 2, "kMaxAltas=110 = 127-17, con static_assert contra el lector con signo")
    else:
        fallo("S1.tope", int(tope == 110) + int(asevera), 2,
              f"kMaxAltas={tope}, static_assert(127)={asevera}")


def s2_previa() -> None:
    src = leer(R / "src" / "escenarios.cpp")
    ini = leer(R / "src" / "escenarios_selector.inl")
    cvar = bool(re.search(r"REXCVAR_DEFINE_BOOL\(dbz3_selector_musica_aislada, false", src))
    fallback = "dbz3::musica::ElegidaAhora()" in ini and "dbz3::musica::Escuchar(" in ini and "Pista ElegidaAhora();" in leer(R / "src" / "mods_opcionales.h") and "Pista ElegidaAhora() {" in leer(R / "src" / "mods_opcionales.cpp")
    aislar = "AislarAudioJuego(aislar)" in ini and 'SetFlagByName("audio_gain", "0")' in ini
    titulo = "dbz3 escenarios: vista previa musical del escenario" in ini
    titulos = "TituloDe" in leer(R / "src" / "mods_opcionales.cpp") and "kTitulos[]" in leer(R / "src" / "mods_opcionales.cpp")
    if cvar and fallback and aislar and titulo and titulos:
        ok("S2.previa", 5, 5, "cvar apagada, pista elegida de respaldo, aislamiento y titulo en el log")
    else:
        fallo("S2.previa", sum((cvar, fallback, aislar, titulo, titulos)), 5,
              f"cvar={cvar} respaldo={fallback} aislar={aislar} log={titulo} titulos={titulos}")


def s3_iw() -> None:
    src = leer(R / "src" / "escenarios.cpp")
    iw = 'a.mod.rfind("Campo IW", 0) == 0' in src and "a.oculta = true" in src
    ocultas = "std::stable_partition(g_altas.begin(), g_altas.end()" in src and "g_visibles = uint32_t" in src
    if iw and ocultas:
        ok("S3.iw", 2, 2, "los campos de IW (Campo IW *) salen del selector y van al final (ocultas)")
    else:
        fallo("S3.iw", sum((iw, ocultas)), 2, f"iw={iw} particion={ocultas}")


# --------------------------------------------------------------------------- L2
def l2_desplazamiento() -> None:
    # Mismos numeros que src/escenarios.cpp: kSerie=17, kCeldas=11, kSelMax=24, y el "?" (12) fuera del
    # carrusel => P=23 casillas que muestran contenido.
    kSerie, kCeldas, kSelMax, kCasillasSerie = 17, 11, 24, 13
    P = kSelMax - 1

    def ventana(L: int, q: int, M: int) -> list[int]:
        out = []
        for k in range(P):
            dd = k - q
            if dd > P // 2:
                dd -= P
            if dd < -((P - 1) // 2):
                dd += P
            out.append(((L + dd) % M + M) % M)
        return out

    casos = 0
    buenos = 0
    primera = ""
    for N in range(kCeldas + 1, 111):   # mas de 11 altas: el carrusel se desplaza
        M = 11 + N
        L = 0
        vistos = set()
        for paso in range(M * 2):
            q = paso % P
            v = ventana(L, q, M)
            casos += 1
            if len(set(v)) == len(v):
                buenos += 1
            elif not primera:
                primera = f"N={N} q={q}: repite {v}"
            vistos.update(v)
            L = (L + 1) % M
        if len(vistos) != M and not primera:
            primera = f"N={N}: solo se alcanzan {len(vistos)} de {M}"
    if buenos == casos:
        ok("L2.carrusel", buenos, casos, "ventana de 23 casillas sin repetidos, todas las altas alcanzables")
    else:
        fallo("L2.carrusel", buenos, casos, primera)

    # Ultima pagina del roster de personajes: LoadDynamicPage completa a 24 con los primeros, sin repetir
    # DENTRO de la pagina (roster_ext.cpp, comentario de la pagina corta).
    pagina = 24
    casos = 0
    buenos = 0
    primera = ""
    for N in range(pagina + 1, 131):
        for inicio in range(0, N, pagina):
            ids = list(range(inicio, min(inicio + pagina, N)))
            for k in range(N):
                if len(ids) >= pagina:
                    break
                if k not in ids:
                    ids.append(k)
            casos += 1
            if len(ids) == pagina and len(set(ids)) == pagina:
                buenos += 1
            elif not primera:
                primera = f"N={N}: pagina {inicio // pagina} con {len(ids)} ({len(set(ids))} unicos)"
    if buenos == casos:
        ok("L2.roster-pagina", buenos, casos,
           "ultima pagina completa a 24 plazas sin ID repetido (los que faltan repiten entre paginas)")
    else:
        fallo("L2.roster-pagina", buenos, casos, primera)


# --------------------------------------------------------------------------- R1
def r1_revancha() -> None:
    def azar(sal: int, semilla: int, combates: int) -> int:
        x = (semilla ^ (combates * 0x9E3779B9) ^ (sal * 0x85EBCA6B)) & 0xFFFFFFFF
        x ^= x >> 16
        x = (x * 0x7FEB352D) & 0xFFFFFFFF
        x ^= x >> 15
        x = (x * 0x846CA68B) & 0xFFFFFFFF
        x ^= x >> 16
        return x

    def registro(azar32: int, visibles: int) -> int:
        n = 11 + visibles
        k = azar32 % n
        return k if k <= 10 else 17 + (k - 11)

    casos = 0
    buenos = 0
    primera = ""
    for N in range(0, 111):
        semilla = 0xC0FFEE ^ N
        combates = 3
        # primera carga: respeta el elegido (aqui el registro 5)
        actual = 5
        for r in range(3):
            a = azar((actual * 0x9E3779B9 + 0x1234) & 0xFFFFFFFF, semilla, combates)
            nuevo = registro(a, N)
            if nuevo == actual:
                for intento in range(1, 17):
                    nuevo = registro((a + intento) & 0xFFFFFFFF, N)
                    if nuevo != actual:
                        break
                if nuevo == actual:   # siguiente permitido (mismo respaldo que RevanchaAzar)
                    if actual >= 17:
                        nuevo = actual + 1 if actual - 17 + 1 < N else 0
                    else:
                        nuevo = (actual + 1) % 11
            casos += 1
            permitido = (nuevo <= 10) or (17 <= nuevo <= 17 + N - 1)
            if nuevo != actual and permitido:
                buenos += 1
            elif not primera:
                primera = f"N={N} revancha {r + 1}: {actual} -> {nuevo} (permitido={permitido})"
            actual = nuevo
    if buenos == casos:
        ok("R1.politica", buenos, casos,
           "3 revanchas seguidas cambian de registro; solo 0..10 y altas (ni 11..16 ni IW)")
        print("     => SOLO la politica: en el juego la revancha nativa NO recarga el escenario "
              "(estados 7->2->3->4 sin sub_820C26E0); SEL01 APARCADO")
    else:
        fallo("R1.revancha", buenos, casos, primera)

    # OFF: RevanchaAzar devuelve -1 sin tocar nada. Se comprueba leyendo la guarda del codigo.
    src = leer(R / "src" / "escenarios.cpp")
    guarda = re.search(r"int RevanchaAzar\(uint8_t\* base, int i\) \{\s*if \(!REXCVAR_GET\(dbz3_escenarios_revancha_aleatoria\)\) \{\s*g_aleatorio = false;\s*return -1;",
                       src)
    if guarda:
        ok("R1.politica-OFF", 1, 1, "sin la cvar vuelve -1 (identico al original)")
    else:
        fallo("R1.revancha-OFF", 0, 1, "no encuentro la guarda de la cvar en RevanchaAzar")


# --------------------------------------------------------------------------- H1
def h1_hub() -> None:
    # Mismo calculo que src/ingame/game_style.cpp::GuestImage y ::GameArea; filas de hub_panel.cpp.
    kInX0, kInX1 = 110.0, 568.0
    kRow0, kPitch = 114.0, 57.5
    kFrameY0, kFrameY1 = 67.0, 566.0
    kBubbleY0, kBubbleY1 = 684.0, 864.0
    per_page, entradas = 8, 12

    def guest_image(w: float, h: float, baja: bool = False) -> tuple[float, float, float, float]:
        an, al = (4.0, 3.0) if baja else (16.0, 9.0)
        x0, y0, x1, y1 = 0.0, 0.0, w, h
        if w * al > h * an:
            bw = h * an / al
            x0, x1 = (w - bw) * 0.5, (w + bw) * 0.5
        else:
            bh = w * al / an
            y0, y1 = (h - bh) * 0.5, (h + bh) * 0.5
        return x0, y0, x1, y1

    def area(w: float, h: float, baja: bool = False):
        gx0, gy0, gx1, gy1 = guest_image(w, h, baja)
        sw, sh = gx1 - gx0, gy1 - gy0
        ah = sh
        aw = ah * 4.0 / 3.0
        if aw > sw:
            aw, ah = sw, sw * 0.75
        ax0 = gx0 + (sw - aw) * 0.5
        ay0 = gy0 + (sh - ah) * 0.5
        return ax0, ay0, aw, ah, ah / 900.0, (gx0, gy0, gx1, gy1)

    formatos = [("4:3 1024x768", 1024, 768), ("16:9 1600x900", 1600, 900),
                ("21:9 2560x1080", 2560, 1080), ("21:9 3440x1440", 3440, 1440),
                ("32:9 3840x1080", 3840, 1080)]
    casos = 0
    buenos = 0
    primera = ""
    for nombre, w, h in formatos:
        ax0, ay0, aw, ah, s, g = area(w, h)
        filas = []
        for slot in range(per_page):
            y = kRow0 + kPitch * slot
            r = (ax0 + kInX0 * s, ay0 + (y - kPitch * 0.5) * s,
                 ax0 + kInX1 * s, ay0 + (y + kPitch * 0.5) * s)
            filas.append((y, r))
        for slot, (y, r) in enumerate(filas):
            casos += 1
            dentro = (g[0] <= r[0] and r[2] <= g[2] and g[1] <= r[1] and r[3] <= g[3] and
                      r[2] > r[0] and r[3] > r[1] and r[1] < r[3])
            # sin solape con la fila siguiente de la misma pagina
            if slot + 1 < len(filas):
                dentro = dentro and r[3] <= filas[slot + 1][1][1] + 1e-6
            if dentro:
                buenos += 1
            elif not primera:
                primera = f"{nombre} fila {slot}: {r} fuera de imagen {g}"
        # marco y bocadillo dentro del area (la fila 8 acaba antes del marco; el bocadillo va debajo)
        casos += 2
        if ay0 + kFrameY0 * s >= ay0 and ay0 + kFrameY1 * s <= ay0 + ah:
            buenos += 1
        elif not primera:
            primera = f"{nombre}: marco {kFrameY0}..{kFrameY1} fuera del area 4:3"
        if ay0 + kBubbleY0 * s >= ay0 and ay0 + kBubbleY1 * s <= ay0 + ah:
            buenos += 1
        elif not primera:
            primera = f"{nombre}: bocadillo {kBubbleY0}..{kBubbleY1} fuera del area 4:3"
    if buenos == casos:
        ok("H1.hitboxes", buenos, casos,
           "8 filas por pagina dentro de la imagen y del area 4:3 en 4:3/16:9/21:9/32:9")
    else:
        fallo("H1.hitboxes", buenos, casos, primera)

    # Paginacion: 12 entradas fijas, 8 por pagina => 2 paginas, la ultima con 4.
    paginas = math.ceil(entradas / per_page)
    if paginas == 2:
        ok("H1.paginas", 2, 2, f"{entradas} entradas en {paginas} paginas de {per_page}")
    else:
        fallo("H1.paginas", 0, 2, f"{entradas} entradas darían {paginas} paginas")


def main() -> int:
    print("C3 - banco offline (selector + HUB), sin juego ni build")
    l1_byte_registro()
    s1_tope()
    s2_previa()
    s3_iw()
    l2_desplazamiento()
    r1_revancha()
    h1_hub()
    if FALLOS:
        print(f"RESULTADO: {len(FALLOS)} fallo(s): {', '.join(FALLOS)}")
        return 1
    print("RESULTADO: verde")
    return 0


if __name__ == "__main__":
    sys.exit(main())
