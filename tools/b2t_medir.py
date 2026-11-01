"""Medidas propias solB2T. Evidencia fuera del repositorio, sin alterar estados base."""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import struct
import shutil
import subprocess
import threading
import ctypes
import time
import sys
import zipfile

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
TMP = Path(r"D:\tmp\solB2T")
INSTANCIA = "luna"


def guardar(nombre, datos):
    TMP.mkdir(parents=True, exist_ok=True)
    (TMP / nombre).write_text(json.dumps(datos, indent=2, ensure_ascii=False, default=str), encoding="utf-8")


def inventario():
    from PIL import Image, ImageDraw
    carpetas = [Path(r"E:\DBZ3HD_trabajo\R7\emu\pcsx2_luna\sstates"),
                Path(r"E:\DBZ3HD_trabajo\A2nave_emu\pcsx2\sstates"),
                Path(r"E:\DBZ3HD_trabajo\R7\emu\pcsx2\sstates")]
    filas, fotos = [], []
    for carpeta in carpetas:
        for p in sorted(carpeta.glob("SLES-51839*.p2s")):
            with zipfile.ZipFile(p) as z:
                ram = z.read("eeMemory.bin")
                u = lambda a: struct.unpack_from("<I", ram, a)[0]
                filas.append(dict(ruta=str(p), sha256=hashlib.sha256(p.read_bytes()).hexdigest(),
                    campo=u(0x371550), rueda=hex(u(0x3a4cb8)), burbuja=hex(u(0x3a4cd0)),
                    res=[hex(u(a)) for a in (0x699da4, 0x699da8, 0x699dac)]))
                if "Screenshot.png" in z.namelist():
                    im = Image.open(io.BytesIO(z.read("Screenshot.png"))).convert("RGB")
                    im.thumbnail((320, 240))
                    tile = Image.new("RGB", (330, 270))
                    tile.paste(im, (0, 24))
                    ImageDraw.Draw(tile).text((2, 2), carpeta.parts[-3] + " " + p.name.rsplit(".", 2)[-2], fill="white")
                    fotos.append(tile)
    guardar("estados.json", filas)
    hoja = Image.new("RGB", (330 * 4, 270 * ((len(fotos) + 3) // 4)))
    for i, im in enumerate(fotos):
        hoja.paste(im, (330 * (i % 4), 270 * (i // 4)))
    hoja.save(TMP / "estados.jpg")
    print(len(filas), "estados identificados")


def recursos():
    sys.path[:0] = [str(ROOT / "mod center hd"), str(ROOT / "mod center hd/pasarela")]
    from iso import Iso
    from pasarela import Afs
    iso = Iso(r"D:\DBZ3HD\ps2_games\DragonBall Z - Budokai 2 (Europe) (En,Fr,De,Es,It).iso")
    # ELF: resolver gp del segmento REGINFO, sin estimarlo por las direcciones de datos.
    elf = iso.read("/SLES_518.39")
    phoff = struct.unpack_from("<I", elf, 28)[0]
    phsz, phn = struct.unpack_from("<HH", elf, 42)
    gps = []
    for i in range(phn):
        p = struct.unpack_from("<8I", elf, phoff + i * phsz)
        if p[0] == 0x70000000:
            gps.append(hex(struct.unpack_from("<I", elf, p[1] + 20)[0]))
    secciones = struct.unpack_from("<HH", elf, 46)
    guardar("elf.json", dict(gp=gps, secciones=secciones, sha256=hashlib.sha256(elf).hexdigest()))
    print("gp", gps, "secciones", secciones)
    with zipfile.ZipFile(r"E:\DBZ3HD_trabajo\A2nave_emu\pcsx2\sstates\SLES-51839 (FE09A7CC).04.p2s") as z:
        ram = z.read("eeMemory.bin")
        for nombre in z.namelist():
            if "reg" in nombre.lower():
                b = z.read(nombre)
                print("Registros",nombre,len(b),"gp candidato",hex(struct.unpack_from("<I",b,28*16)[0]) if len(b)>=464 else "corto")
        ptr = struct.unpack_from("<I", ram, 0x3a42d0)[0] & 0x1fffffff
        print("RPC sonido", hex(ptr), ram[ptr:ptr+16].hex())


def materiales():
    import numpy as np
    filas = []
    for p in Path(r"D:\DBZ3HD\sol\ingame_ui\nave_vista").glob("sk_*.sk"):
        d = p.read_bytes()
        if d[:4] != b"HMS3":
            continue
        nt, ntri, nb = struct.unpack_from("<3I", d, 4)
        off = 20
        for _ in range(nt):
            w, h = struct.unpack_from("<2I", d, off)
            off += 8 + w*h*4
        off += nb * 116
        t = np.frombuffer(d, dtype=np.dtype([("v", "<f4", (3, 9)), ("tex", "<u4"), ("flags", "<u4")]), count=ntri, offset=off)
        off += ntri*116
        n_orig = struct.unpack_from("<I", d, off)[0]
        off += 4
        unicos = []
        for i in range(n_orig):
            n = struct.unpack_from("<I", d, off)[0]
            off += 4
            if n == 1:
                unicos.append(i)
            off += n*8
        normales = t["v"][:, :, 3:6]
        mod = np.linalg.norm(normales, axis=2)
        filas.append(dict(ruta=str(p), sha256=hashlib.sha256(d).hexdigest(), triangulos=ntri,
            textura_constante=unicos, tris_constantes=int(np.isin(t["tex"], unicos).sum()),
            normal_min=float(mod.min()), normal_max=float(mod.max()),
            normales_no_unitarias=int((abs(mod-1)>0.01).sum())))
    guardar("materiales.json", filas)
    for f in filas:
        print(Path(f["ruta"]).name, "tris material constante", f["tris_constantes"], "normales", f["normal_min"], f["normal_max"])


def audio_resumen():
    for p in Path(r"D:\_PARA_BORRAR_CLAUDE\solB2T\captura_estados").glob("*.p2s"):
        with zipfile.ZipFile(p) as z:
            ram = z.read("eeMemory.bin")
        u = lambda a: struct.unpack_from("<I",ram,a)[0]
        campo = u(0x371550)
        if campo not in (16668,16768):
            continue
        visual = u(0x3a4a20) & 0x1fffffff
        filas=[]
        for i in range(1,5):
            off=0x69b7a0+i*0x24
            v=visual+(i-1)*0x50
            filas.append(dict(i=i,logico=ram[off:off+0x24].hex(),visual=ram[v:v+0x98].hex(),
                estado_88=struct.unpack_from("<5H",ram,v+0x88)))
        guardar(f"audio_ram_{campo}.json",dict(estado=str(p),campo=campo,visual=hex(visual),filas=filas))
        print(campo,hex(visual),[(f["i"],f["logico"][:24],f["estado_88"]) for f in filas])


def mandos():
    dll = ctypes.WinDLL("xinput1_4.dll")
    for i in range(4):
        b = ctypes.create_string_buffer(16)
        r = dll.XInputGetState(i,b)
        print("XInput", i, "estado", r, "paquete/botones/gatillos/palancas", struct.unpack("<IHBB4h",b.raw))


def resumir():
    for p in sorted(TMP.glob("nave_recorridos_validos_gp_p*.json")):
        d=json.loads(p.read_text(encoding="utf-8"))
        fs=d["filas"]
        cambios=[]
        for f in fs:
            if not cambios or cambios[-1][1]!=f["datos"]:
                cambios.append((f["cuadro"],f["datos"]))
        print(p.name,"campos",len(fs),"completa",d.get("completa"),"cambios",cambios)


def limites():
    """Ejecuta el codigo de producto contra el limite de una traza RAM independiente."""
    traza = Path(r"D:\tmp\solP3\premio_saltar_cuenta.json")
    filas = json.loads(traza.read_text(encoding="utf-8"))["traza"]
    cambios = []
    for f in filas:
        fun = f["datos"]["resultado"]["fun"]
        if not cambios or cambios[-1]["funcion"] != fun:
            cambios.append(dict(campo=f["cuadro"], funcion=fun))
    espera = next(f["campo"] for f in cambios if f["funcion"] == "0x263670")
    barra = next(f["campo"] for f in cambios if f["funcion"] == "0x262dd0")
    campos_original = barra - espera
    assert campos_original == 41, cambios
    fuente = (ROOT / "src/pasarela_nave.cpp").read_text(encoding="utf-8")
    a = fuente.index("struct Res {")
    b = fuente.index("// La ficha de la capsula tras el cartel", a)
    bloque = fuente[a:b]
    cabecera = r'''
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#define REXLOG_INFO(...) ((void)0)
struct Premio { int id, kili; };
struct Prueba { int pago; };
Prueba kPruebas[] = {{25},{2},{4},{3}};
int g_prueba=0, g_cuenta=1, g_kili=0;
float g_cuadro=0;
std::atomic<int> g_hz_b2{50};
const std::vector<Premio>& Premios() { static std::vector<Premio> p{{0,100},{1,10000}}; return p; }
int Maximo(int) { return 1; }
int Tienes(int) { return 0; }
void Dar(const Premio&, int) {}
void GuardarProgreso(const std::string&, int) {}
void Efecto(int, int) {}
'''
    main = r'''
int main() {
  // Cuatro pagos, cero, premio exacto, barra saturada y vuelta con premios.
  for (g_prueba=0; g_prueba<4; ++g_prueba) {
    EmpezarResultado();
    PasoResultado(1.2f,true);
    int f=0;
    while(g_res.fase==1 && f<100) { PasoResultado(1.2f,false); ++f; }
    if(f!=41) return 10+g_prueba;
    std::printf("prueba %d: pausa %d campos\n",g_prueba+1,f);
  }
  g_prueba=0; g_cuenta=0; EmpezarResultado(); PasoResultado(1.2f,false);
  for(int i=0;i<40;++i) if(PasoResultado(1.2f,false)) return 20;
  if(!PasoResultado(1.2f,false)) return 21;
  g_prueba=0; g_cuenta=4; g_kili=0; EmpezarResultado();
  PasoResultado(1.2f,true);
  for(int i=0;i<41;++i) PasoResultado(1.2f,false);
  PasoResultado(1.2f,true);
  if(g_res.fase!=3 || g_res.barra!=100 || g_res.ganado!=0) return 22;
  PasoResultado(1.2f,true); // cerrar premio
  if(g_res.ganados[0]!=1 || g_res.quedan!=1) return 23;
  g_cuenta=1; g_kili=10000; EmpezarResultado(); g_res.quedan=0;
  g_res.sig=2; g_res.fase=2; g_res.ganado=25;
  PasoResultado(1.2f,true);
  if(g_res.barra!=10000 || g_res.ganado!=0 || g_res.fase!=4) return 24;
  EmpezarResultado(); g_res.fase=2; g_res.sig=2; g_res.barra=10000; g_res.ganado=25;
  PasoResultado(1.2f,true);
  if(g_res.barra!=25 || g_res.ganado!=0) return 25;
  g_hz_b2=60; EmpezarResultado(); PasoResultado(1.0f,true);
  for(int i=0;i<40;++i) { PasoResultado(1.0f,false); if(g_res.fase!=1) return 26; }
  PasoResultado(1.0f,false); if(g_res.fase!=2) return 27;
  std::puts("cero / premio exacto / saturacion / vuelta: PASS");
}
'''
    TMP.mkdir(parents=True, exist_ok=True)
    cpp, exe = TMP / "resultado_oraculo.cpp", TMP / "resultado_oraculo.exe"
    cpp.write_text(cabecera+bloque+main, encoding="utf-8")
    build = subprocess.run([r"C:\Program Files\LLVM\bin\clang++.exe", "-std=c++17", str(cpp), "-o", str(exe)], capture_output=True, text=True)
    print(build.stdout, build.stderr)
    build.check_returncode()
    r = subprocess.run([str(exe)], capture_output=True, text=True)
    guardar("resultado_limites.json", dict(original=dict(traza=str(traza), sha256=hashlib.sha256(traza.read_bytes()).hexdigest(),
        cambios=cambios, campos=campos_original), antes=dict(campos=34, error_campos=7,
        metodo="codigo previo: espera += 1.2, sale al superar 40"), despues=dict(codigo=r.returncode, salida=r.stdout,
        fuente_sha256=hashlib.sha256(fuente.encode()).hexdigest(), metodo="bloque Res/PasoResultado del producto, compilado; sin ventana")))
    print(r.stdout, r.stderr)
    r.check_returncode()


def plaza_original(P, s):
    """Reservar el lanzamiento ANTES de esperar capacidad evita inanicion por otras tandas."""
    from ctypes import wintypes as W
    k = P.K32
    k.FindFirstChangeNotificationW.argtypes = [W.LPCWSTR, W.BOOL, W.DWORD]
    k.FindFirstChangeNotificationW.restype = W.HANDLE
    k.FindCloseChangeNotification.argtypes = [W.HANDLE]
    k.WaitForMultipleObjects.argtypes = [W.DWORD, ctypes.POINTER(W.HANDLE), W.BOOL, W.DWORD]
    k.WaitForMultipleObjects.restype = W.DWORD
    while True:
        aviso = k.FindFirstChangeNotificationW(str(P.BASE), False, 2)
        if aviso in (None, ctypes.c_void_p(-1).value):
            raise ctypes.WinError()
        try:
            try:
                P.LANZANDO.mkdir()
                break
            except FileExistsError:
                k.WaitForSingleObject(aviso, 0xffffffff)
        finally:
            k.FindCloseChangeNotification(aviso)
    candado = P.LANZANDO.stat().st_ino
    parar = threading.Event()
    def latido():
        while not parar.wait(10):
            if P.LANZANDO.exists() and P.LANZANDO.stat().st_ino == candado:
                os.utime(P.LANZANDO, None)
    hilo = threading.Thread(target=latido, daemon=True)
    hilo.start()
    try:
        while True:
            datos = subprocess.run(["powershell", "-NoProfile", "-Command",
                "@(Get-Process dbz3,pcsx2 -ErrorAction SilentlyContinue) | ForEach-Object {$_.Id}"],
                capture_output=True, creationflags=subprocess.CREATE_NO_WINDOW, text=True, check=True).stdout.split()
            propio = s.nuestro_pid()
            if len(datos) < __import__("pruebas").TOPE_JUEGOS and not propio:
                return candado
            ids = [str(propio)] if propio else datos
            hs = [h for pid in ids if (h := k.OpenProcess(0x100000, False, int(pid)))]
            if not hs:
                raise RuntimeError("Sin acceso a la notificacion de fin de proceso")
            print("Turno de lanzamiento reservado; esperando cierre natural de", ids, flush=True)
            try:
                arr = (W.HANDLE * len(hs))(*hs)
                if k.WaitForMultipleObjects(len(hs), arr, False, 0xffffffff) == 0xffffffff:
                    raise ctypes.WinError()
            finally:
                for h in hs:
                    k.CloseHandle(h)
    except BaseException:
        if P.LANZANDO.exists() and P.LANZANDO.stat().st_ino == candado:
            P.LANZANDO.rmdir()
        raise
    finally:
        parar.set()
        hilo.join()


def trazar_sonidos(p):
    """Sonda reversible solo en la VM propia: jal -> stub -> funcion original.

    Sigue gancho_regs de sonda.py; registra argumentos y VBlank EE, no estima
    momentos por el color de una imagen ni por el ultimo buffer RPC compartido.
    """
    sys.path[:0] = [str(ROOT / "mod center hd")]
    from iso import Iso
    elf = Iso(r"D:\DBZ3HD\ps2_games\DragonBall Z - Budokai 2 (Europe) (En,Fr,De,Es,It).iso").read("/SLES_518.39")
    shoff = struct.unpack_from("<I", elf, 32)[0]
    shsz, shn = struct.unpack_from("<HH", elf, 46)
    jal = 0x0c000000 | (0x147260 >> 2)
    sitios = []
    for j in range(shn):
        sh = struct.unpack_from("<10I", elf, shoff + j*shsz)
        if sh[1] != 1 or not sh[2] & 4:
            continue
        for off in range(0, sh[5], 4):
            if struct.unpack_from("<I", elf, sh[4]+off)[0] == jal:
                va = sh[3]+off
                if p.u32(va) != jal:
                    raise RuntimeError(f"La llamada {va:x} no coincide con el ELF")
                sitios.append(va)
    if not sitios:
        raise RuntimeError("ELF sin secciones ejecutables con llamadas de sonido")
    buf, n = 0xe0000, 512
    stub = buf+0x80+n*32
    ocupado = p.leer(buf, 0x5000)
    if any(ocupado):
        raise RuntimeError("La zona de sonda no esta libre; no se sobrescribe")
    def i(op, rs, rt, imm):
        return (op << 26) | (rs << 21) | (rt << 16) | (imm & 65535)
    c = [i(15,0,8,buf>>16), i(35,8,9,0), i(12,9,10,n-1),
         (10<<16)|(11<<11)|(5<<6), (11<<21)|(8<<16)|(11<<11)|0x21,
         i(9,11,11,0x80), i(43,11,4,0), i(43,11,5,4), i(43,11,6,8), i(43,11,31,12),
         i(15,0,12,0x37), i(35,12,12,0x1550), i(43,11,12,16), i(43,11,9,20),
         i(9,9,9,1), 0x08000000|(0x147260>>2), i(43,8,9,0)]
    p.escribir(stub, struct.pack("<"+"I"*len(c), *c))
    for va in sitios:
        p.escribir(va, struct.pack("<I", 0x0c000000|(stub>>2)))
    return sitios, jal, buf, n


def leer_sonidos(p, gancho):
    sitios, jal, buf, n = gancho
    total = p.u32(buf)
    eventos = []
    for seq in range(max(0,total-n),total):
        canal, banco, id_, ra, campo, orden, _, _ = struct.unpack("<8I", p.leer(buf+0x80+(seq%n)*32,32))
        if orden != seq:
            raise RuntimeError("Anillo de sonido incoherente")
        eventos.append(dict(canal=canal, banco=banco, id=id_, ra=hex(ra), campo=campo, secuencia=seq))
    return dict(total=total, completa=total<=n, sitios=len(sitios), eventos=eventos)


def original(semilla, nombre, n, tecla=None):
    import pcsx2_cuadros as Q
    import pruebas as P
    from types import SimpleNamespace
    os.environ["DBZ3_PCSX2_CUADROS_TMP"] = str(TMP)
    os.environ["DBZ3_PCSX2_CUADROS_RESPALDO"] = str(TMP / "estados")
    os.environ["DBZ3_PCSX2_INSTANCIA"] = INSTANCIA
    s = Q.conectar(INSTANCIA)
    candado = plaza_original(P, s)
    parar_arranque = threading.Event()
    def mantener_arranque():
        while not parar_arranque.wait(10):
            if P.LANZANDO.exists() and P.LANZANDO.stat().st_ino == candado:
                os.utime(P.LANZANDO, None)
    latido_arranque = threading.Thread(target=mantener_arranque, daemon=True)
    latido_arranque.start()
    propio = None
    preparado = False
    gancho = None
    try:
        Q.preparar(s)
        preparado = True
        p = s.lanzar("b2", str(semilla))
        propio = s.nuestro_pid()
        p.s.close()
        if P.LANZANDO.exists() and P.LANZANDO.stat().st_ino == candado:
            P.LANZANDO.rmdir()
        parar_arranque.set()
        latido_arranque.join()
        P._a_pantalla_secundaria(SimpleNamespace(pid=propio, poll=lambda: None))
        # HWND propio fijo: enumerar tasklist dos veces POR campo encarecia las
        # series y podia perder una ventana momentaneamente oculta.
        from ctypes import wintypes as W
        hwnd = s.ventana_juego(propio)
        s.u32dll.IsWindow.argtypes = [W.HWND]
        s.u32dll.GetWindowThreadProcessId.argtypes = [W.HWND,ctypes.POINTER(W.DWORD)]
        class CuadrosPropios(Q.Cuadros):
            def tecla(self,vk,abajo):
                self.s.TURNO.validar()
                pid = W.DWORD()
                s.u32dll.GetWindowThreadProcessId(hwnd,ctypes.byref(pid))
                if not s.u32dll.IsWindow(hwnd) or pid.value != propio:
                    raise RuntimeError("La ventana propia de PCSX2 se ha cerrado")
                sc = s.u32dll.MapVirtualKeyW(vk,0)
                lp = 1|(sc<<16)|((1<<24) if 0x25<=vk<=0x28 else 0)|(0 if abajo else 3<<30)
                if not s.u32dll.PostMessageW(hwnd,0x100 if abajo else 0x101,vk,lp):
                    raise ctypes.WinError()
        c = CuadrosPropios(s, 0x371550)
        c.pausa()
        if nombre.startswith("audio_"):
            gancho = trazar_sonidos(c.p)
        def observar(p):
            u = p.u32
            datos = dict(rueda=hex(u(0x3a4cb8)), burbuja=hex(u(0x3a4cd0)))
            datos["resultado"] = [u(a) for a in (0x699da4, 0x699da8, 0x699dac)]
            datos["valores"] = [u(a + 0x30) if 0x100000 < a < 0x2000000 else None for a in datos["resultado"]]
            # TABLA de tareas de la escena: registros originales que revelan los cambios de callback.
            datos["nave"] = p.leer(0x699d60, 0x70).hex()
            datos["gp_datos"] = p.leer(0x3a4c00, 0x100).hex()
            datos["pad"] = p.leer(0x3b43a0, 0x20).hex()
            datos["fichas"] = p.leer(0x69b7c4, 4*0x24).hex()
            visual = u(0x3a4a20) & 0x1fffffff
            if 0x100000 < visual < 0x2000000-0x110:
                datos["visual_1"] = p.leer(visual,0x110).hex()
            rpc = u(0x3a42d0) & 0x1fffffff  # gp=0x3ABCF0; alias EE no cacheado
            datos["rpc"] = list(struct.unpack("<3I", p.leer(rpc, 12))) if 0x100000 < rpc < 0x2000000 else None
            return datos
        if nombre.startswith("nave_recorridos_validos"):
            def observar_nave(p):
                addrs = [0x3a4900,0x3a4ce0,0x3a4cb8,0x3a4cbc,0x3a4cc8,0x3a4ccc,0x3a492c]
                vs = p.lote([(a,4) for a in addrs])
                vs[:6] = [v & 0x1fffffff for v in vs[:6]]
                out = dict(planta=(vs[-1]&255)+1)
                dirs=[]
                names=[]
                for nombre_,ptr,off in [("nave",vs[0],0x1c),("motor",vs[1],0x24),
                    ("rueda0",vs[2],0x54),("rueda1",vs[3],0x54),("figura0",vs[4],0x48),("figura1",vs[5],0x48)]:
                    out[nombre_]=None
                    if 0x100000<ptr<0x2000000-0x100:
                        dirs.append((ptr+off,4)); names.append(nombre_)
                for k,v in zip(names,p.lote(dirs)):
                    out[k]=v
                return out
            def tramo(planta,fase,vk,cond,minimo=0):
                filas=c.medir(3,observar_nave,tecla=vk,mantener=3) if vk is not None else [dict(cuadro=c.numero(),datos=observar_nave(c.p))]
                ini=filas[0]["cuadro"]
                for _ in range(1000):
                    if c.numero()-ini>=minimo and cond(filas[-1]["datos"]):
                        guardar(f"{nombre}_p{planta}_{fase}.json",dict(filas=filas,completa=True))
                        return
                    c.paso()
                    filas.append(dict(cuadro=c.numero(),datos=observar_nave(c.p)))
                    if len(filas)%20==0:
                        guardar(f"{nombre}_p{planta}_{fase}.json",dict(filas=filas,completa=False))
                raise RuntimeError(f"No se alcanzo {fase}: {filas[-1]}")
            for planta in range(1,5):
                if planta>1:
                    tramo(planta,"elegir_planta",0x28,lambda d:d["planta"]==planta,4)
                tramo(planta,"rueda",0x5a,lambda d:d["motor"]==4 and d["figura0"]==2)
                c.captura(TMP/f"{nombre}_p{planta}_rueda.png")
                tramo(planta,"menu",0x5a,lambda d:True,30)
                # Nave 1P reutiliza la ranura 0 al pasar a COM (traza: 9 -> 13
                # -> 14 -> 6, y figura 2 -> 1 -> 2); no crea una segunda rueda.
                tramo(planta,"COM",0x5a,lambda d:d["rueda0"]==6 and d["figura0"]==2)
                tramo(planta,"burbuja",0x5a,lambda d:d["motor"]==7)
                # Motor 7 significa que la burbuja existe, no que ya acepte
                # pad: su entrada de anillo dura 45 campos. Conservar el reposo
                # posterior por separado y no perder el primer triangulo.
                reposo=c.medir(60,observar_nave)
                guardar(f"{nombre}_p{planta}_burbuja_reposo.json",dict(filas=reposo,completa=True))
                c.captura(TMP/f"{nombre}_p{planta}_burbuja.png")
                tramo(planta,"atras_COM",0x53,lambda d:d["rueda0"]==6 and d["motor"]==4)
                tramo(planta,"atras_menu",0x53,lambda d:True,30)
                tramo(planta,"atras_rueda",0x53,lambda d:d["rueda0"]==6)
                tramo(planta,"atras_torre",0x53,lambda d:d["nave"]==0x264790,70)
                print("Recorrido original validado planta",planta,flush=True)
            c.p.s.close()
            return
        if nombre == "nave_recorridos":
            import importlib.util
            ruta = ROOT / "studies 5/ENTREGAS/solP/original_3.py"
            sp = importlib.util.spec_from_file_location("b2t_original_3",ruta)
            modulo = importlib.util.module_from_spec(sp)
            sp.loader.exec_module(modulo)
            for planta in range(1,5):
                if planta > 1:
                    c.medir(4,modulo.observar,tecla=0x28,mantener=3)
                actual = c.p.leer(0x3a492c,1)[0]+1
                if actual != planta:
                    raise RuntimeError(f"La torre original selecciona {actual}, se esperaba {planta}")
                fases = [("rueda",0x5a,100),("menu",0x5a,30),("COM",0x5a,100),("burbuja",0x5a,150),
                         ("atras_COM",0x53,90),("atras_menu",0x53,90),("atras_rueda",0x53,40),("atras_torre",0x53,100)]
                for fase,vk,campos in fases:
                    filas = c.medir(campos,modulo.observar,tecla=vk,mantener=3)
                    guardar(f"nave_original_p{planta}_{fase}.json",dict(planta=planta,fase=fase,filas=filas,
                        consecutiva=all(b["cuadro"]-a["cuadro"]==1 for a,b in zip(filas,filas[1:]))))
                    if fase in ("rueda","burbuja"):
                        c.captura(TMP / f"nave_original_p{planta}_{fase}.png")
                print("Original planta",planta,"recorrido por campos conservado",flush=True)
            c.p.s.close()
            return
        inicio = c.captura(TMP / (nombre + "_inicio.png"))
        filas = c.medir(n, observar, tecla=tecla, mantener=3,
            al_campo=lambda f: guardar(nombre + ".json", dict(inicio=inicio, filas=f, completa=len(f) == n+1)))
        fin = c.captura(TMP / (nombre + "_fin.png"))
        guardar(nombre + ".json", dict(inicio=inicio, fin=fin, filas=filas, completa=True))
        if gancho:
            guardar(nombre + "_sonidos.json", leer_sonidos(c.p, gancho))
            for va in gancho[0]:
                c.p.escribir(va, struct.pack("<I", gancho[1]))
            gancho = None
        c.p.s.close()
        print(nombre, len(filas), "campos consecutivos")
    finally:
        parar_arranque.set()
        latido_arranque.join()
        if propio and s.nuestro_pid() == propio:
            s.main(["--instancia", INSTANCIA, "cerrar"])
        if preparado and not s.nuestro_pid():
            respaldo = TMP / ("PCSX2_" + INSTANCIA + ".ini.original")
            if respaldo.exists():
                archivo = Path(r"D:\_PARA_BORRAR_CLAUDE\solB2T")
                archivo.mkdir(parents=True, exist_ok=True)
                ini = Path(s.EMU) / "inis/PCSX2.ini"
                shutil.move(str(ini), archivo / ("PCSX2_usado_" + str(time.time_ns()) + ".ini"))
                shutil.move(str(respaldo), ini)
        if P.LANZANDO.exists() and P.LANZANDO.stat().st_ino == candado:
            P.LANZANDO.rmdir()


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("accion", choices=["inventario", "recursos", "materiales", "limites", "audio-resumen", "mandos", "resumir", "original", "lanzar"])
    ap.add_argument("--estado", type=Path)
    ap.add_argument("--nombre", default="original")
    ap.add_argument("-n", type=int, default=70)
    ap.add_argument("--tecla", type=lambda v: int(v, 0))
    a = ap.parse_args()
    if a.accion == "lanzar":
        TMP.mkdir(parents=True, exist_ok=True)
        with (TMP / (a.nombre + ".log")).open("x", encoding="utf-8") as out:
            p = subprocess.Popen([sys.executable, str(Path(__file__).resolve()), "original",
                *sys.argv[2:]], stdout=out, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                creationflags=subprocess.DETACHED_PROCESS | subprocess.CREATE_NEW_PROCESS_GROUP)
        print("Medida original en cola, PID", p.pid)
    elif a.accion == "original":
        original(a.estado, a.nombre, a.n, a.tecla)
    elif a.accion == "recursos":
        recursos()
    elif a.accion == "materiales":
        materiales()
    elif a.accion == "limites":
        limites()
    elif a.accion == "audio-resumen":
        audio_resumen()
    elif a.accion == "mandos":
        mandos()
    elif a.accion == "resumir":
        resumir()
    else:
        inventario()
