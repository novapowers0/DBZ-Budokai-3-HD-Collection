"""Medidas solB2U: datos del jugador y trazas originales, sin arte en el repo."""
import argparse
import contextlib
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time
import zipfile

sys.dont_write_bytecode = True
os.environ["DBZ3_AGENTE"] = "solB2U"
ROOT = Path(__file__).resolve().parents[1]
TMP = Path(r"D:\tmp\solB2U")


def guardar(nombre, datos):
    TMP.mkdir(parents=True, exist_ok=True)
    (TMP / nombre).write_text(json.dumps(datos, indent=2, ensure_ascii=False, default=str), encoding="utf-8")


def inventario():
    filas = []
    carpetas = [Path(r"D:\tmp\solB2T\estados\captura_estados"),
                Path(r"E:\DBZ3HD_trabajo\A2nave_emu\pcsx2\sstates")]
    for carpeta in carpetas:
        for p in sorted(carpeta.glob("*.p2s")):
            with zipfile.ZipFile(p) as z:
                ram = z.read("eeMemory.bin")
            u = lambda a: struct.unpack_from("<I", ram, a)[0]
            filas.append(dict(ruta=str(p), campo=u(0x371550), escena=u(0x3a3e70),
                nave=hex(u(0x3a4900)), sha256=hashlib.sha256(p.read_bytes()).hexdigest()))
    guardar("estados.json", filas)
    print(json.dumps(filas, indent=2))
    resumen = {}
    for p in Path(r"D:\tmp\solB2T").glob("nave_recorridos_validos_gp3_p1_*.json"):
        d = json.loads(p.read_text(encoding="utf-8"))
        cambios = []
        for f in d["filas"]:
            if not cambios or cambios[-1][1] != f["datos"]:
                cambios.append([f["cuadro"], f["datos"]])
        resumen[p.name] = cambios
    guardar("retorno_antes.json", resumen)
    print(json.dumps(resumen, indent=2))


def recursos():
    sys.path[:0] = [str(ROOT / "mod center hd"), str(ROOT / "mod center hd/pasarela")]
    from iso import Iso
    from pasarela import Afs
    afs = Afs(Iso(r"D:\DBZ3HD\ps2_games\DragonBall Z - Budokai 2 (Europe) (En,Fr,De,Es,It).iso"), "/USR/DATA_CMN.AFS")
    ama = afs.entry(397)
    u = lambda b, a: struct.unpack_from("<I", b, a)[0]
    tabla = u(ama, 12)
    dur = {i: list(struct.unpack_from("<2I", ama, tabla+64*i+0x20)) for i in [0,1,0x21,0x23,0x3c]}
    ydw = afs.entry(387)
    n, off = u(ydw, 0x20), u(ydw, 0x24)
    casillas = []
    for i in range(min(n, 4)):
        p = off+0x34*i
        casillas.append(dict(indice=i, raw=ydw[p:p+0x34].hex()))
    shen = afs.entry(328)
    hijos = []
    for i in range(u(shen, 0x10)):
        off_, tam, tipo, slot = struct.unpack_from("<4I", shen, u(shen, 0x14)+16*i)
        hijos.append(dict(i=i, tipo=tipo, slot=slot, bytes=tam, magic=shen[off_:off_+4].decode("ascii", "replace")))
    guardar("recursos.json", dict(ama=dur, casillas=casillas, shen_hijos=hijos))
    print(json.dumps(dict(ama=dur, casillas=casillas, shen_hijos=hijos), indent=2))


def spx():
    """Ejecuta el bytecode con la VM real del producto, sin ventana ni cambiarla."""
    sys.path[:0] = [str(ROOT / "mod center hd"), str(ROOT / "mod center hd/pasarela")]
    from iso import Iso
    from pasarela import Afs
    afs = Afs(Iso(r"D:\DBZ3HD\ps2_games\DragonBall Z - Budokai 2 (Europe) (En,Fr,De,Es,It).iso"), "/USR/DATA_CMN.AFS")
    fuente = (ROOT / "src/pasarela_b2.cpp").read_text(encoding="utf-8")
    vm = fuente[fuente.index("namespace spx {"):fuente.index("// Ejecuta el #SPX de una ventana")]
    marca = "    switch (cod) {"
    assert vm.count(marca) == 1
    vm = vm.replace(marca, '    std::printf("%d %x %d %d %d %d\\n", sal.cuadros, cod, a(0), a(1), a(2), a(3));\n' + marca)
    cabecera = """#include <algorithm>
#include <array>
#include <deque>
#include <vector>
#include <map>
#include <unordered_map>
#include <functional>
#include <string>
#include <fstream>
#include <iterator>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <climits>
#include <cstdio>
#define REXLOG_INFO(...) ((void)0)
struct MptCelda { int bloque, entrada, patron, flags; float ancho, x, y, capa; };
"""
    main = """
int main(int argc, char** argv) {
  std::ifstream in(argv[1], std::ios::binary);
  const std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), {});
  spx::Maquina m(d);
  if (!m.Reservar()) return 1;
  m.PonerGuion(); m.PonerTablaExt(0x379640); m.Lanzar(0);
  for (int i=0; i<2000 && m.Vivo(); ++i) m.Cuadro();
}
"""
    TMP.mkdir(parents=True, exist_ok=True)
    cpp, exe = TMP / "spx_vm.cpp", TMP / "spx_vm.exe"
    cpp.write_text(cabecera + vm + main, encoding="utf-8")
    datos = TMP / "spx329.bin"
    datos.write_bytes(afs.entry(329))
    r = subprocess.run([r"C:\Program Files\LLVM\bin\clang++.exe", "-std=c++20", str(cpp), "-o", str(exe)], capture_output=True, text=True)
    if r.returncode:
        print(r.stderr)
        r.check_returncode()
    r = subprocess.run([str(exe), str(datos)], capture_output=True, text=True, check=True)
    (TMP / "spx329_llamadas.txt").write_text(r.stdout, encoding="utf-8")
    eventos = []
    for ln in r.stdout.splitlines():
        f, cod, *args = ln.split()
        if int(cod, 16) in (0x30,0x33,0x36,0x37,0x3a,0x50,0x60,0xf0,0x100):
            eventos.append(dict(cuadro=int(f), builtin=cod, argumentos=list(map(int,args))))
    guardar("spx329_medida.json", dict(eventos=eventos,
        fuente_sha256=hashlib.sha256(fuente.encode()).hexdigest(),
        guion_sha256=hashlib.sha256(datos.read_bytes()).hexdigest(),
        limite="VM de producto sobre bytecode del jugador: los builtins 3D siguen ignorados; no es traza nativa PCSX2"))
    print(json.dumps(eventos, indent=2))


def figuras():
    filas = []
    for p in sorted(Path(r"D:\DBZ3HD\smoke_e\ingame_ui\nave_vista").glob("sk_*.sk")):
        d = p.read_bytes()
        nt, ntri, nb, alto = struct.unpack_from("<IIIf", d, 4)
        off = 20
        for _ in range(nt):
            w, h = struct.unpack_from("<2I", d, off)
            off += 8+w*h*4
        huesos = []
        for _ in range(nb):
            padre = struct.unpack_from("<i", d, off)[0]
            nombre = d[off+4:off+28].split(b"\0")[0].decode("latin1")
            con = struct.unpack_from("<I", d, off+76)[0]
            huesos.append(dict(nombre=nombre, padre=padre, giro_nativo=bool(con)))
            off += 116
        filas.append(dict(fid=p.stem, alto=alto, triangulos=ntri, huesos=nb,
            giros_nativos=sum(h["giro_nativo"] for h in huesos),
            pelo=[h for h in huesos if any(k in h["nombre"] for k in ("HAIR", "KAMI"))]))
    guardar("figuras_cache.json", dict(figuras=filas,
        limite="Inventario del cache HD; no acredita escala ni pelo contra el roster original completo"))
    print(len(filas), "figuras en cache;", sum(f["giros_nativos"] == 0 for f in filas), "sin giros nativos; roster completo NO certificado")


def espera():
    sys.path.insert(0, str(ROOT / "mod center hd"))
    from iso import Iso
    import capstone as C
    b = Iso(r"D:\DBZ3HD\ps2_games\DragonBall Z - Budokai 2 (Europe) (En,Fr,De,Es,It).iso").read("/SLES_518.39")
    def datos(va, n):
        phoff = struct.unpack_from("<I", b, 28)[0]
        sz, count = struct.unpack_from("<HH", b, 42)
        for i in range(count):
            p = struct.unpack_from("<8I", b, phoff+i*sz)
            if p[0] == 1 and p[2] <= va and va+n <= p[2]+p[4]:
                return b[p[1]+va-p[2]:p[1]+va-p[2]+n]
        raise ValueError(hex(va))
    tabla = struct.unpack("<I", datos(0x379640+4*0x10, 4))[0]
    funciones = struct.unpack("<16I", datos(tabla, 64))
    motor = C.Cs(C.CS_ARCH_MIPS, C.CS_MODE_MIPS64 | C.CS_MODE_LITTLE_ENDIAN)
    texto = "tabla " + hex(tabla) + "\n" + str([hex(f) for f in funciones]) + "\n"
    for va in (0x225850, 0x225930, 0x225990):
        texto += "\n" + hex(va) + "\n"
        texto += "\n".join(f"{i.address:x}: {i.mnemonic} {i.op_str}" for i in motor.disasm(datos(va, 0x90), va)) + "\n"
    (TMP / "espera_elf.txt").write_text(texto, encoding="utf-8")
    print(texto)


def recuperar():
    import pcsx2_cuadros as Q
    s = Q.conectar("luna")
    if s.nuestro_pid():
        raise RuntimeError("Hay una VM en luna: no se cambia su configuracion")
    ini = Path(s.EMU) / "inis/PCSX2.ini"
    respaldo = TMP / "nave2/PCSX2_luna.ini.original"
    if respaldo.exists():
        archivo = TMP / "ini_usados"
        archivo.mkdir(exist_ok=True)
        shutil.move(str(ini), archivo / (str(time.time_ns()) + ".ini"))
        shutil.move(str(respaldo), ini)
    print("Instancia luna cerrada; configuracion restaurada")


@contextlib.contextmanager
def original(estado):
    import b2t_medir as B
    import pcsx2_cuadros as Q
    import pruebas as P
    from types import SimpleNamespace
    os.environ["DBZ3_PCSX2_CUADROS_TMP"] = str(TMP)
    os.environ["DBZ3_PCSX2_CUADROS_RESPALDO"] = str(TMP / "estados")
    os.environ["DBZ3_PCSX2_INSTANCIA"] = "luna"
    s = Q.conectar("luna")
    candado = B.plaza_original(P, s)
    pid = None
    preparado = False
    try:
        Q.preparar(s)
        preparado = True
        p = s.lanzar("b2", str(estado))
        pid = s.nuestro_pid()
        p.s.close()
        if P.LANZANDO.exists() and P.LANZANDO.stat().st_ino == candado:
            P.LANZANDO.rmdir()
        P._a_pantalla_secundaria(SimpleNamespace(pid=pid, poll=lambda: None))
        c = Q.Cuadros(s, 0x371550)
        c.pausa()
        try:
            yield c
        finally:
            c.p.s.close()
    finally:
        if pid and s.nuestro_pid() == pid:
            s.main(["--instancia", "luna", "cerrar"])
        if preparado and not s.nuestro_pid():
            ini = Path(s.EMU) / "inis/PCSX2.ini"
            archivo = TMP / "ini_usados"
            archivo.mkdir(exist_ok=True)
            shutil.move(str(ini), archivo / (str(time.time_ns()) + ".ini"))
            shutil.move(str(TMP / "PCSX2_luna.ini.original"), ini)
        if P.LANZANDO.exists() and P.LANZANDO.stat().st_ino == candado:
            P.LANZANDO.rmdir()


def audio(estado):
    import b2t_medir as B
    with original(estado) as c:
        gancho = B.trazar_sonidos(c.p)
        def observar(p):
            # La primera pieza usa visual+0, NO visual+0x50; alias EE normalizado.
            visual = p.u32(0x3a4a20) & 0x1fffffff
            b = p.leer(visual, 0x98)
            logico = p.leer(0x69b7c4, 0x24)
            return dict(estado=logico[0], personaje=struct.unpack_from("<H", logico, 4)[0],
                casillas=struct.unpack_from("<2H", logico, 8), contador=struct.unpack_from("<H", logico, 14)[0],
                anim=struct.unpack_from("<f", b, 0x10)[0],
                icono=struct.unpack_from("<5H", b, 0x88), visual=hex(visual))
        filas = c.medir(100, observar, tecla=0x5a, mantener=3)
        guardar("audio_original.json", dict(filas=filas, sonidos=B.leer_sonidos(c.p, gancho)))
        cambios = []
        for f in filas:
            if not cambios or cambios[-1]["datos"] != f["datos"]:
                cambios.append(f)
        print(json.dumps(cambios, indent=2))
        for va in gancho[0]:
            c.p.escribir(va, struct.pack("<I", gancho[1]))


def nave(estado):
    with original(estado) as c:
        def observar(p):
            u = p.u32
            out = dict(planta=(u(0x3a492c) & 255) + 1)
            for nombre, a, off in (("nave", 0x3a4900, 0x1c), ("motor", 0x3a4ce0, 0x24),
                    ("rueda0", 0x3a4cb8, 0x54), ("figura0", 0x3a4cc8, 0x48)):
                ptr = u(a) & 0x1fffffff
                out[nombre] = u(ptr + off) if 0x100000 < ptr < 0x1ffff00 else None
            return out
        medidas = []
        def tramo(planta, fase, tecla, cond, minimo=0, maximo=240):
            filas = c.medir(3, observar, tecla=tecla, mantener=2) if tecla else [dict(cuadro=c.numero(), datos=observar(c.p))]
            ini = filas[0]["cuadro"]
            while c.numero() - ini < maximo:
                if c.numero() - ini >= minimo and cond(filas[-1]["datos"]):
                    guardar(f"nave_p{planta}_{fase}.json", dict(filas=filas, completa=True))
                    medidas.append(dict(planta=planta, fase=fase, campos=c.numero()-ini))
                    return
                c.paso()
                filas.append(dict(cuadro=c.numero(), datos=observar(c.p)))
            guardar(f"nave_p{planta}_{fase}.json", dict(filas=filas, completa=False))
            raise RuntimeError(f"No alcanzo {fase}: {filas[-1]}")
        try:
            if observar(c.p)["nave"] != 0x264790:
                tramo(1, "inicio_torre", 0x5a, lambda d: d["nave"] == 0x264790, 61)
            for planta in range(1, 5):
                if planta > 1:
                    tramo(planta, "elegir", 0x28, lambda d: d["planta"] == planta, 3)
                tramo(planta, "rueda", 0x5a, lambda d: d["motor"] == 4 and d["figura0"] == 2)
                tramo(planta, "menu", 0x5a, lambda d: d["rueda0"] == 9, 25)
                tramo(planta, "COM", 0x5a, lambda d: d["rueda0"] == 6 and d["motor"] == 4, 60)
                tramo(planta, "burbuja", 0x5a, lambda d: d["motor"] == 7, 60)
                tramo(planta, "reposo", None, lambda d: True, 60)
                c.captura(TMP / f"nave_p{planta}_burbuja.png")
                tramo(planta, "atras_COM", 0x53, lambda d: d["motor"] == 4 and d["rueda0"] == 6, 60)
                # Motor 6 es SALIDA de la seleccion, no menu 1P. El triangulo
                # del COM destruye la rueda y vuelve a torre (FUN_002640E0).
                tramo(planta, "atras_torre", 0x53, lambda d: d["nave"] == 0x264790, 61)
                print("Recorrido original", planta, "completo", flush=True)
        finally:
            guardar("nave_resumen.json", dict(medidas=medidas, combate=False))


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("accion", choices=["inventario", "audio", "nave", "recursos", "recuperar", "spx", "figuras", "espera"])
    ap.add_argument("--estado", type=Path)
    ap.add_argument("--salida", type=Path)
    a = ap.parse_args()
    if a.salida:
        TMP = a.salida
    if a.accion in ("inventario", "recursos", "recuperar", "spx", "figuras", "espera"):
        globals()[a.accion]()
    else:
        globals()[a.accion](a.estado)
