"""Pelicula de fotogramas de un clip con el modelo real (KIT-08, generador de animaciones, sin interfaz).

Cada fotograma sale del render toon por software (model_render) con la pose del clip (studio_core.anim_locals),
con el MISMO encuadre en todo el clip (caja de todas las poses), para que al pasar fotogramas no baile.
Cache en disco (en el PC del jugador; no se envia arte): una carpeta por clave (personaje, modelo, clip, vista,
px, paso y fecha/tamano de los ficheros de origen); la segunda llamada no renderiza.

  pelicula(personaje, codigo, vista="lado", px=240, modelo=None, anm=None, paso=1) -> [rutas PNG]
  fotograma_fino(personaje, codigo, frame, vista="lado", px=720, ...)             -> ruta PNG
  marcas(personaje, codigo, anm=None)                                              -> [(frame, "golpe"|"efecto")]
  tira(rutas, out, n=8)                                                            -> PNG con n fotogramas en fila

modelo: None (el del personaje), un fid de data_cmn.afs o la ruta de un .bin/.amb de un mod.
anm:    None (moveset del juego), un fid o la ruta de un override (p.ej. el geom.bin que deja aplicar_receta).
Cache:  env DBZ3_KIT_CACHE o <cache del Kit (vistas.cache_dir, la de las fotos de escenario/portada)>/pelicula.
Prueba: python "mod center hd/kit/prueba_pelicula.py"
"""
import hashlib
import json
import os
import sys
from concurrent.futures import ProcessPoolExecutor

HD = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if HD not in sys.path:
    sys.path.insert(0, HD)
import biblioteca_anim as ba  # noqa: E402  (prepara studio/ y awo_tools/ en sys.path)
from kit import base  # noqa: E402

sc = ba.sc
VISTAS = {"frente": 0.0, "3/4": 45.0, "lado": 90.0}
VERSION = 1                 # sube si cambia el dibujo: invalida la cache vieja
CACHE = os.environ.get("DBZ3_KIT_CACHE")    # None: la cache del Kit (KIT-09)


def _cache(c):
    if c or CACHE:
        return c or CACHE
    from kit import vistas  # noqa: PLC0415
    return os.path.join(vistas.cache_dir(), "pelicula")


def _bin(x, us, que):
    """bytes HD de un fid o fichero (LZX y PS2 se convierten) + firma (ruta, fecha, tamano) del origen."""
    if isinstance(x, int):
        afs = os.path.join(sc.us_dir(us), "data_cmn.afs")
        st = os.stat(afs)
        return sc.hd_entry(x, us), [afs, x, int(st.st_mtime), st.st_size]
    if os.path.isdir(x):                     # carpeta de override: el primer fichero (como el runtime)
        x = os.path.join(x, sorted(os.listdir(x))[0])
    st = os.stat(x)
    with open(x, "rb") as fh:
        raw = fh.read()
    b = base.lzx_bytes(raw, False) if raw[:4] == base.LZX_MAGIC else raw
    if b[:4] == b"#AMB" and sc.endian(b) == "<":
        import ps2hd  # noqa: PLC0415
        b = bytes(ps2hd.convert_block(b))
    if b[:4] != b"#AMB":
        raise ValueError("%s: %s no es un #AMB" % (que, x))
    return b, [os.path.abspath(x), int(st.st_mtime), st.st_size]


def _origen(personaje, codigo, modelo, anm, us):
    p = ba._persona(personaje)
    mb, fm = _bin(p["modelo"] if modelo is None else modelo, us, "modelo")
    ab, fa = _bin(p["anm"] if anm is None else anm, us, "anm")
    csk, e = sc.csk_of(ab)
    info = sc.code_info(csk, e, int(codigo)) if csk else None
    if info is None:
        raise ValueError("%s no tiene el codigo %#x" % (p["nombre"], int(codigo)))
    if info[1] != 3:
        raise ValueError("%#x usa el banco comun %d: sin preview (solo clips propios)" % (int(codigo), info[1]))
    acm, _ = sc.anim_pool(ab)
    return mb, acm, e, info, [fm, fa]


# ------------------------------------------------ render (tambien en procesos hijos)
_W = {}


def _modelo(mb):
    import altura  # noqa: PLC0415
    from model_render import Model  # noqa: PLC0415
    key = hashlib.sha1(mb).hexdigest()
    if key not in _W:
        _W.clear()
        _W[key] = (Model(mb), altura.skeleton(mb))
    return _W[key]


def _encuadre(mb, acm, e, anim, nf, seguir=False):
    """([centro de cada fotograma], extension). Fijo: la caja de TODAS las poses (se ve cuanto se desplaza).
    seguir (KIT-10): el centro de la pose de cada fotograma y el tamano de la pose mas grande; un movimiento que se
    desplaza mucho ya no sale diminuto (miniaturas y panel Original)."""
    import numpy as np  # noqa: PLC0415
    m, skel = _modelo(mb)
    cajas = []
    for f in range(nf):
        m.local = sc.anim_locals(skel, acm, e, anim, f)
        P, _ = m.posed(None)
        cajas.append((P.min(0), P.max(0)))
    if seguir:
        return [((lo + hi) / 2).tolist() for lo, hi in cajas], float(max(max(hi - lo) for lo, hi in cajas)) or 1.0
    lo = np.min([c[0] for c in cajas], axis=0)
    hi = np.max([c[1] for c in cajas], axis=0)
    return [((lo + hi) / 2).tolist()] * nf, float(max(hi - lo)) or 1.0


def _lote(mb, acm, e, anim, frames, yaw, px, centros, ext, rutas):
    """centros: el de cada fotograma de `frames` (de _encuadre)."""
    from PIL import Image  # noqa: PLC0415
    from model_render import render, rot_y  # noqa: PLC0415
    m, skel = _modelo(mb)
    for f, ruta, centro in zip(frames, rutas, centros):
        m.local = sc.anim_locals(skel, acm, e, anim, f)
        img = render(m, size=(px, px), view=rot_y(yaw), center=centro, scale=0.92 * px / ext, ss=2, outline=1.2)
        Image.fromarray(img).save(ruta + ".tmp.png")
        os.replace(ruta + ".tmp.png", ruta)
    return len(frames)


def _clave(*partes):
    return hashlib.sha1(json.dumps([VERSION] + list(partes), sort_keys=True, default=str).encode()).hexdigest()[:20]


def pelicula(personaje, codigo, vista="lado", px=240, modelo=None, anm=None, paso=1, us=None, cache=None,
             trabajadores=None, seguir=False):
    """Rutas PNG de los fotogramas 0, paso, 2*paso... del clip `codigo` del personaje (fondo transparente).
    trabajadores: procesos para renderizar (None = automatico: 1 en clips cortos/pequenos).
    seguir: encuadre que sigue al personaje (ver _encuadre)."""
    if vista not in VISTAS:
        raise ValueError("vista: %s" % "/".join(VISTAS))
    mb, acm, e, (anim, _pool, _m), firma = _origen(personaje, codigo, modelo, anm, us)
    nf = ba.frames_de(acm, e, anim) or 1
    d = os.path.join(_cache(cache), _clave(personaje, modelo, anm, int(codigo), vista, px, paso, firma,
                                           *(["seguir"] if seguir else [])))
    frames = list(range(0, nf, max(1, int(paso))))
    rutas = [os.path.join(d, "f%03d.png" % f) for f in frames]
    hecho = os.path.join(d, "hecho.json")
    if os.path.isfile(hecho) and all(os.path.isfile(r) for r in rutas):
        return rutas
    os.makedirs(d, exist_ok=True)
    centros, ext = _encuadre(mb, acm, e, anim, nf, seguir)
    falta = [(f, r) for f, r in zip(frames, rutas) if not os.path.isfile(r)]
    n = trabajadores or (1 if len(falta) < 16 else min(4, os.cpu_count() or 1))   # 0.23 s/f a 240 px: 79 f = 18 s en 1, 5 s en 4
    yaw = VISTAS[vista]
    if n <= 1:
        _lote(mb, acm, e, anim, [f for f, _ in falta], yaw, px, [centros[f] for f, _ in falta], ext,
              [r for _, r in falta])
    else:
        trozos = [falta[k::n] for k in range(n)]
        with ProcessPoolExecutor(n) as ex:
            list(ex.map(_lote, *zip(*[(mb, acm, e, anim, [f for f, _ in t], yaw, px, [centros[f] for f, _ in t], ext, [r for _, r in t])
                                      for t in trozos if t])))
    with open(hecho, "w", encoding="utf-8") as fh:
        json.dump(dict(personaje=personaje, codigo=int(codigo), vista=vista, px=px, frames=nf, paso=paso,
                       encuadre=[centros[0], ext], seguir=seguir, origen=firma), fh, indent=1)
    return rutas


def fotograma_fino(personaje, codigo, frame, vista="lado", px=720, modelo=None, anm=None, us=None, cache=None,
                   seguir=False):
    """Un fotograma a mas resolucion (mismo encuadre que la pelicula), para cuando se pausa."""
    mb, acm, e, (anim, _pool, _m), firma = _origen(personaje, codigo, modelo, anm, us)
    nf = ba.frames_de(acm, e, anim) or 1
    frame = int(frame) % nf
    d = os.path.join(_cache(cache), _clave(personaje, modelo, anm, int(codigo), vista, px, "fino", firma,
                                           *(["seguir"] if seguir else [])))
    ruta = os.path.join(d, "f%03d.png" % frame)
    if not os.path.isfile(ruta):
        os.makedirs(d, exist_ok=True)
        centros, ext = _encuadre(mb, acm, e, anim, nf, seguir)
        _lote(mb, acm, e, anim, [frame], VISTAS[vista], px, [centros[frame]], ext, [ruta])
    return ruta


def marcas(personaje, codigo, anm=None, us=None):
    """Marcas del #CSK del codigo: [(frame, "golpe")] (AP tipo 1, golpe) y [(frame, "efecto")] (AP tipo 7)."""
    _mb, _acm, _e, (_a, _p, ms), _f = _origen(personaje, codigo, None, anm, us)
    return sorted({(f, "golpe" if t == 1 else "efecto") for f, t in ms})


def tira(rutas, out, n=8, fondo=(40, 44, 52, 255)):
    """PNG con n fotogramas repartidos por la pelicula, en fila (evidencia y miniaturas)."""
    from PIL import Image  # noqa: PLC0415
    sel = [rutas[round(k * (len(rutas) - 1) / max(1, n - 1))] for k in range(min(n, len(rutas)))]
    ims = [Image.open(r) for r in sel]
    w, h = ims[0].size
    hoja = Image.new("RGBA", (w * len(ims), h), fondo)
    for k, im in enumerate(ims):
        hoja.alpha_composite(im.convert("RGBA"), (w * k, 0))
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    hoja.save(out)
    return out
