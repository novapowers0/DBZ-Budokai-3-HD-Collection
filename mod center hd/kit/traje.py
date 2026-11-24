# -*- coding: utf-8 -*-
"""«Cambiar traje» sin interfaz (KIT-06): formas del personaje, un modelo por forma, reetiquetado al esqueleto del
personaje destino (como importar.traje_desde_port), validador ANTES de montar y vista previa toon.

Origen de cada forma: ruta de un archivo de modelo | "juego:N" (modelo del juego, entrada N de data_cmn) | None.
    python kit/traje.py vista <us> <id> <forma> <origen> <carpeta_salida> [tono]   (lo lanza la ventana en 2.o plano)
"""
from __future__ import annotations

import hashlib
import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VERSION = 1                       # sube si cambia el dibujo de la vista: invalida la cache vieja
NOMBRES = {"Normal": ("Base", "Base"), "Kaiohken": ("Kaioken", "Kaioken"), "SSJ": ("Super Saiyan", "Super Saiyan"),
           "SSJ2": ("Super Saiyan 2", "Super Saiyan 2"), "SSJ3": ("Super Saiyan 3", "Super Saiyan 3"),
           "SSJ4": ("Super Saiyan 4", "Super Saiyan 4")}
_DB = []
_REQ = {}


def _rb():
    for p in (HERE, os.path.join(os.path.dirname(HERE), "awo_tools")):
        if p not in sys.path:
            sys.path.insert(0, p)
    import roster_build  # noqa: PLC0415
    return roster_build


def _db():
    if not _DB:
        with open(os.path.join(HERE, "roster_db.json"), encoding="utf-8") as fh:
            _DB.append(json.load(fh))
    return _DB[0]


def _variantes():
    out = {}
    try:
        with open(os.path.join(HERE, "catalog_b3.cat"), encoding="utf-8", errors="replace") as fh:
            for ln in fh:
                p = ln.rstrip("\r\n").split("|")
                if len(p) > 3 and p[0].isdigit():
                    out[int(p[0])] = p[3]
    except OSError:
        pass
    return out


def formas(cid):
    """[{f, id, mi, nat, es, en, otro}] de un personaje del juego, en el orden de sus transformaciones (roster_db
    form_models; como importar.traje_desde_port). otro = la forma es otro personaje por dentro (Goku SSJ4 = 91)."""
    db = _db()
    dn = next((e for e in db["ids"] if e["id"] == cid), None)
    if not dn or not dn.get("models"):
        return []
    per = max(1, dn.get("models_per_costume", 1))
    var = _variantes()
    out = []
    for f, (fid, mi) in enumerate(dn.get("form_models") or [[cid, k] for k in range(per)]):
        if fid == cid:
            nat = dn["models"][min(mi, per - 1)]
        else:
            o = db.get("form_ids", {}).get(str(fid), {})
            nat = o.get("models", [None])[min(mi, max(0, o.get("models_per_costume", 1) - 1))]
        v = (var.get(nat) or "").split()                  # variante del catalogo: "Sin armadura SSJ" -> SSJ
        tok = v[-1] if v and v[-1] in NOMBRES else ("Normal" if f == 0 else "")
        es, en = NOMBRES.get(tok, ("Forma %d" % (f + 1), "Form %d" % (f + 1)))
        n = sum(1 for x in out if x["es"].split(" (")[0] == es)
        if n:
            es, en = "%s (%d)" % (es, n + 1), "%s (%d)" % (en, n + 1)
        out.append({"f": f, "id": fid, "mi": mi, "nat": nat, "es": es, "en": en, "otro": fid != cid})
    return out


def origenes_port(mods, port, cid):
    """Qué archivo del port usaría cada forma con «importar.py traje» (misma regla que traje_desde_port); None en las
    formas que son otro personaje (se quedan con el modelo del juego). Solo para la vista previa."""
    import tomllib  # noqa: PLC0415
    src = os.path.join(mods, port)
    with open(os.path.join(src, "personaje.toml"), "rb") as fh:
        cfg = tomllib.load(fh).get("personaje", {})
    files = cfg.get("modelos", [])
    forma = cfg.get("modelo_forma") or list(range(len(files)))
    out = []
    for f, fo in enumerate(formas(cid)):
        k = forma[min(f, len(forma) - 1)] if forma else 0
        out.append(None if fo["otro"] or not files else os.path.join(src, files[min(k, len(files) - 1)]))
    return out


# ------------------------------------------------------------------------------------------ validador
class NoEncaja(Exception):
    """Modelo que no sirve para esa forma: frase llana (es, en) + detalle tecnico."""

    def __init__(self, es, en, detalle=""):
        super().__init__(es)
        self.es, self.en, self.detalle = es, en, detalle


def _hd(origen, work, cmn):
    rb = _rb()
    try:
        if str(origen).startswith("juego:"):
            return rb.to_hd(cmn.entry(int(str(origen)[6:])), work)
        with open(origen, "rb") as fh:
            return rb.to_hd(fh.read(), work)
    except (OSError, ValueError, IndexError) as ex:
        raise NoEncaja("Ese archivo no es un modelo del juego (o está dañado).",
                       "That file is not a game model (or it is damaged).", str(ex)) from ex


def _etiquetas(hd, work):
    try:
        lab = _rb().skeleton(hd, work)[0]
    except Exception as ex:  # noqa: BLE001 -- modelo raro: se explica, no se cae la ventana
        raise NoEncaja("No se puede leer el cuerpo de ese modelo.", "That model's body cannot be read.", str(ex)) from ex
    return [x.decode("latin1") if isinstance(x, bytes) else str(x) for x in lab]


def _norm(labels, pre):
    return {x.replace(pre + "_", "@_") for x in labels}


def requeridos(cid, work, cmn):
    """Huesos que mueven las animaciones del personaje (los sin X de su forma base), con el prefijo como '@'."""
    if cid not in _REQ:
        lab = _etiquetas(_hd("juego:%d" % formas(cid)[0]["nat"], work, cmn), work)
        pre = _rb().prefix(lab)
        _REQ[cid] = {x for x in _norm(lab, pre) if not x.startswith("X")}
    return _REQ[cid]


def preparar(origen, cid, forma, work, cmn):
    """-> (hd, de, a): el modelo listo para la forma `forma` (reetiquetado si hace falta: de/a = prefijos, iguales si
    no se toca). Lanza NoEncaja si su esqueleto no es el del personaje."""
    rb = _rb()
    from importar import _relabel_awo  # noqa: PLC0415  (la misma funcion que importar.py traje)
    fo = formas(cid)[forma]
    hd = _hd(origen, work, cmn)
    lab = _etiquetas(hd, work)
    have = rb.prefix(lab)
    want = rb.prefix(_etiquetas(_hd("juego:%d" % fo["nat"], work, cmn), work))
    if not have or not want:
        raise NoEncaja("Ese modelo no tiene el esqueleto de un luchador de Budokai 3.",
                       "That model does not have a Budokai 3 fighter skeleton.", "sin prefijo de huesos")
    if have != want:
        if len(have) != len(want):
            raise NoEncaja("Ese modelo es de otro tipo de esqueleto y no se puede adaptar a este personaje.",
                           "That model uses another kind of skeleton and cannot be adapted to this character.",
                           "prefijo de huesos %s_ (hace falta uno de %d letras como %s_)" % (have, len(want), want))
        hd = _relabel_awo(hd, have, want)
    falta = sorted(requeridos(cid, work, cmn) - _norm(lab, have))
    if falta:
        raise NoEncaja("A ese modelo le faltan partes del cuerpo que este personaje necesita para moverse.",
                       "That model is missing body parts this character needs to move.",
                       "huesos que faltan: " + ", ".join(x.replace("@", want) for x in falta))
    return hd, have, want


class Revisor:
    """Valida orígenes con una carpeta de trabajo y data_cmn abiertos una vez. Cache por (origen, fecha, forma)."""

    def __init__(self, us=None):
        rb = _rb()
        self.work = rb.work_dir("kit_traje_")
        self.cmn = rb.Afs(os.path.join(us or rb.default_us(), "data_cmn.afs"), self.work)
        self.memo = {}

    def revisar(self, origen, cid, forma):
        """-> None si vale, o la NoEncaja."""
        k = (_firma(origen), cid, forma)
        if k not in self.memo:
            try:
                preparar(origen, cid, forma, self.work, self.cmn)
                self.memo[k] = None
            except NoEncaja as ex:
                self.memo[k] = ex
        return self.memo[k]


def _firma(origen):
    o = str(origen)
    if o.startswith("juego:"):
        return o
    try:
        st = os.stat(o)
        return "%s|%d|%d" % (os.path.abspath(o), st.st_size, st.st_mtime_ns)
    except OSError:
        return o


# ------------------------------------------------------------------------------------------ escribir el mod
def resolver(cid, por_forma):
    """Qué usa cada forma: [(origen, estado, desde)] con estado 'propio' | 'otra' (usa el de la forma `desde`) |
    'juego' (forma que es otro personaje sin modelo: se queda como está). Error si no hay ningún modelo."""
    fs = formas(cid)
    por_forma = list(por_forma) + [None] * (len(fs) - len(por_forma))
    tiene = [f for f, o in enumerate(por_forma[:len(fs)]) if o]
    if not tiene:
        raise NoEncaja("Elige al menos un modelo.", "Pick at least one model.")
    out = []
    for f, fo in enumerate(fs):
        o = por_forma[f]
        if o:
            out.append((o, "propio", f))
        elif fo["otro"]:
            out.append(("juego:%d" % fo["nat"], "juego", f))
        else:
            cand = [k for k in tiene if not fs[k]["otro"]] or tiene
            antes = [k for k in cand if k < f]          # la forma anterior mas cercana; si no hay, la siguiente
            d = max(antes) if antes else min(cand, key=lambda k: abs(k - f))
            out.append((por_forma[d], "otra", d))
    return out


def escribir(mods, mod, cid, nombre, por_forma, tono=None, us=None, revisor=None):
    """Crea mods/<mod> (modelos/, traje.toml, manifest.txt). Valida TODOS los modelos antes de escribir nada.
    Con un solo modelo para todas las formas y sin reetiquetar, el resultado es el de siempre byte a byte."""
    rv = revisor or Revisor(us)
    plan = resolver(cid, por_forma)
    listos = {}                                           # (firma, forma) -> (hd, de, a)
    for f, (o, est, _d) in enumerate(plan):
        if est == "juego" and tono is None:
            continue
        try:
            listos[f] = preparar(o, cid, f, rv.work, rv.cmn)
        except NoEncaja as ex:
            fo = formas(cid)[f]
            raise NoEncaja("%s: %s" % (fo["es"], ex.es), "%s: %s" % (fo["en"], ex.en), ex.detalle) from ex
    d = os.path.join(mods, mod)
    os.makedirs(os.path.join(d, "modelos"))
    rels, hechos, usados = [], {}, set()
    for f, (o, est, _d) in enumerate(plan):
        o = str(o)
        if f not in listos:
            rels.append(o)
            continue
        hd, de, a = listos[f]
        if o.startswith("juego:") and de == a:
            rels.append(o)                                # del juego y ya con sus huesos: tal cual
            continue
        k = (_firma(o), a)
        if k not in hechos:
            fn = ("juego_%s.amb" % o[6:]) if o.startswith("juego:") else os.path.basename(o)
            base, ext = os.path.splitext(fn)
            n = 2
            while fn.lower() in usados:
                fn, n = "%s_%d%s" % (base, n, ext), n + 1
            usados.add(fn.lower())
            dst = os.path.join(d, "modelos", fn)
            if de == a and not o.startswith("juego:"):
                shutil.copy2(o, dst)                      # como antes: el archivo tal cual
            else:
                with open(dst, "wb") as fh:
                    fh.write(hd)
            hechos[k] = "modelos/" + fn
        rels.append(hechos[k])
    nombre = nombre.replace('"', "").replace("\\", "")
    with open(os.path.join(d, "manifest.txt"), "w", encoding="utf-8") as fh:
        fh.write("name=%s\ndescription=Traje creado con el asistente del Mod Kit\nauthor=\nversion=1.0\ntype=traje\n"
                 % nombre)
    if len(set(rels)) == 1:
        rels = rels[:1]                                   # el mismo para todas (se repite el ultimo)
    with open(os.path.join(d, "traje.toml"), "w", encoding="utf-8") as fh:
        fh.write('[traje]\npersonaje = %d\nnombre = "%s"\nmodelos = [%s]\n' % (
            cid, nombre, ", ".join('"%s"' % r for r in rels)))
        if tono is not None:
            fh.write("tono = %d\n" % int(tono))
    return "traje.toml creado (%d forma(s), %d modelo(s) propio(s))" % (len(plan), len(hechos))


# ------------------------------------------------------------------------------------------ vista previa
def cache_vista(us, cid, forma, origen, tono=None):
    clave = "%s|%s|%d|%d|%s|%s|%d" % (os.path.abspath(us or ""), _firma(origen), cid, forma, tono, VERSION, 1)
    from kit import vistas  # noqa: PLC0415
    return os.path.join(vistas.cache_dir(), "traje_" + hashlib.sha1(clave.encode()).hexdigest()[:16])


def cmd_vista(python, us, cid, forma, origen, salida, tono=None):
    return [python, os.path.abspath(__file__), "vista", us or "-", str(cid), str(forma), str(origen), salida] + (
        [str(tono)] if tono is not None else [])


def vista(us, cid, forma, origen, salida, tono=None):
    """Escribe en `salida`: frente.png, lado.png (render toon, pose de base) y res.json {ok, es, en, detalle}."""
    import numpy as np  # noqa: PLC0415
    from PIL import Image  # noqa: PLC0415
    import model_render as mr  # noqa: PLC0415
    os.makedirs(salida, exist_ok=True)
    rv = Revisor(None if us in ("", "-") else us)
    res = {"ok": True, "es": "", "en": "", "detalle": ""}
    try:
        hd = preparar(origen, cid, forma, rv.work, rv.cmn)[0]
    except NoEncaja as ex:
        res.update(ok=False, es=ex.es, en=ex.en, detalle=ex.detalle)
        try:
            hd = _hd(origen, rv.work, rv.cmn)             # si al menos se lee, se ve igual (con su aviso)
        except NoEncaja:
            hd = None
    if hd is not None:
        if tono is not None:
            import colores  # noqa: PLC0415
            hd = colores.retint_modelo(hd, float(tono))[0]
        m = mr.Model(hd)
        P, _ = m.posed(None)
        lo, hi = P.min(0), P.max(0)
        for nombre, giro in (("frente", 0), ("lado", 90)):
            tam = (300, 340)
            sc = tam[1] * 0.92 / max(hi[1] - lo[1], 1e-6)
            sc = min(sc, tam[0] * 0.95 / max(hi[0] - lo[0], hi[2] - lo[2], 1e-6))
            img = mr.render(m, tam, mr.rot_y(giro), (lo + hi) / 2, sc)
            Image.fromarray(np.asarray(img)).save(os.path.join(salida, nombre + ".png"))
    with open(os.path.join(salida, "res.json"), "w", encoding="utf-8") as fh:
        json.dump(res, fh, ensure_ascii=False)


if __name__ == "__main__":
    if len(sys.argv) >= 7 and sys.argv[1] == "vista":
        sys.path.insert(0, HERE)
        vista(sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), sys.argv[5], sys.argv[6],
              float(sys.argv[7]) if len(sys.argv) > 7 else None)
    else:
        print(__doc__)
