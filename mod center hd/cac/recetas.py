"""CAC-08/CAC-20: recetas de traje del creador de personajes (sin arte).

Una receta es la ficha del heroe (el texto `--ficha` de vista_heroe.py: cuerpo, traje, partes, piezas b3_*, accesorio,
capas, tiras ocultas, colores...) escrita como `traje_cac.toml`. El bin NUNCA viaja: se monta en el PC de quien la abre,
con su propio juego (vista_heroe.py). Mismo texto de ficha = mismo bin.

  python recetas.py autoprueba                  ida y vuelta con 10 recetas al azar (ficha -> toml -> ficha -> codigo -> ficha)
  python recetas.py a-ficha  traje_cac.toml     imprime el texto --ficha
  python recetas.py a-receta "<ficha>" [salida] escribe traje_cac.toml
  python recetas.py codigo   traje_cac.toml     imprime el codigo para compartir
  python recetas.py decodificar "<codigo>" [salida.toml]
"""
import base64
import binascii
import hashlib
import json
import os
import random
import re
import sys
import tomllib
import zlib

VERSION = 1                      # version del formato de la receta (traje_cac.toml y codigo)
PREFIJO = "DBZ3R%d" % VERSION
MAX_FICHA = 4096                 # bytes de ficha: una receta real mide < 600; mas = codigo hostil o roto
# orden de heroe.cpp ArgsFicha (+ ocultar y nombre de ArgsPropio): una ficha de C++ vuelve identica byte a byte
ORDEN = (["cuerpo", "traje", "partes", "xv2", "cv", "rostro", "ojos", "piel", "pelo", "c1", "c2", "logo", "scouter", "pelo_r17",
          "b3_torso", "b3_piernas", "b3_guantes", "b3_botas", "b3_accesorios", "capa1", "capa1_quitar", "capa2", "capa2_quitar", "capa3", "capa3_quitar", "capa4", "capa4_quitar", "texcol",
          "material17", "pintura17", "ropa", "aura_col", "ki_col", "ut", "razas_b3", "orig1", "orig2", "orig3", "orig4", "pelo_fid_cac2", "ocultar",
          "forma1", "estallido1", "forma2", "estallido2", "forma3", "estallido3", "forma4", "estallido4",
          "forma5", "estallido5", "forma6", "estallido6", "forma7", "estallido7", "foto_pose", "foto_yaw", "nombre"])
CADENAS = {"pelo_r17", "texcol", "material17", "pintura17", "nombre", "aura_col", "ki_col", "cv", "capsulas", "excluidas", "donante_txt",
           *(f"forma{i}" for i in range(1, 8)), *(f"estallido{i}" for i in range(1, 8))}
TERNAS = {"ojos", "piel", "c1", "c2"}
CAPAS = {"capa1", "capa2", "capa3", "capa4"}


class ErrorReceta(ValueError):
    """Mensaje de respaldo y clave estable para la interfaz del juego."""
    def __init__(self, mensaje, clave="roto"):
        super().__init__(mensaje)
        self.clave = clave


def _texcol_valido(v):
    if not isinstance(v, str) or len(v) > 2048:
        return False
    if not v:
        return True
    limites = ((0, 2147483647), (0, 5), (0, 360), (-100, 100), (-100, 100))
    for entrada in v.split("|"):
        campos = entrada.split(",")
        if len(campos) != len(limites) or any(
                not re.fullmatch(r"-?[0-9]+", x) or not a <= int(x) <= b
                for x, (a, b) in zip(campos, limites)):
            return False
    return True


def _ficha_a_dict(txt):
    d = {}
    for par in str(txt).split(";"):
        if "=" not in par:
            continue
        k, v = par.split("=", 1)
        k = k.strip()
        if not k:
            continue
        try:
            if k == "partes":
                d[k] = [[int(x) for x in p.split(",")] for p in v.split("|")]
            elif k in TERNAS:
                d[k] = [int(x) for x in v.split(",")]
            elif k == "ocultar" or k.endswith("_quitar"):
                d[k] = [int(x) for x in v.split(",") if x.strip()]
            elif k in CAPAS:
                a, b = v.split(":")
                d[k] = [int(a), int(b)]
            elif k == "xv2":
                d[k] = {n: int(x) for n, x in (q.split(":") for q in v.split(","))}
            elif k == "pelo_r17":
                if re.fullmatch(r"[a-z]+(?:_[a-z]+)+", v) is None or len(v) > 48:
                    raise ValueError("ID de peinado R17 inválido")
                d[k] = v
            elif k in CADENAS:
                d[k] = v
            else:
                d[k] = int(v)
        except ValueError as e:
            if k not in ORDEN and k not in CADENAS:
                raise ErrorReceta("Clave de receta desconocida: «%s»." % k) from e
            raise ErrorReceta("La receta tiene un valor roto en «%s»." % k) from e
        if k == "texcol" and not _texcol_valido(v):
            raise ErrorReceta("La lista de colores por textura no es válida.")
        if k == "pelo_fid_cac2" and d[k] not in (-1, 75, 80):
            raise ErrorReceta("«pelo_fid_cac2» no es un peinado nativo válido.")
        if k in CAPAS and not (-1 <= d[k][0] <= 4095 and 0 <= d[k][1] < 4):
            raise ErrorReceta("«%s» no identifica un modelo y una parte válidos." % k)
        if k in TERNAS and len(d[k]) != 3 or k == "partes" and any(len(p) != 3 for p in d[k]):
            raise ErrorReceta("«%s» requiere ternas de enteros." % k)
    return d


def _dict_a_ficha(d):
    def val(k, v):
        if k == "partes":
            return "|".join(",".join(str(x) for x in p) for p in v)
        if k in TERNAS:
            return ",".join(str(x) for x in v)
        if k == "ocultar" or k.endswith("_quitar"):
            return ",".join(str(x) for x in v)
        if k in CAPAS:
            return "%d:%d" % tuple(v)
        if k == "xv2":
            return ",".join("%s:%d" % (n, x) for n, x in v.items())
        return str(v)
    claves = [k for k in ORDEN if k in d] + [k for k in d if k not in ORDEN]
    campos = []
    for k in claves:
        v = d[k]
        if k in CADENAS and (not isinstance(v, str) or any(c in v for c in ";\r\n\0")):
            raise ErrorReceta("La receta tiene un texto no válido en «%s»." % k)
        def lista_int(xs, n):
            return isinstance(xs, list) and len(xs) == n and all(type(x) is int for x in xs)
        if k in TERNAS and not lista_int(v, 3):
            raise ErrorReceta("«%s» requiere tres enteros." % k)
        if k == "partes" and (not isinstance(v, list) or not v or not all(lista_int(p, 3) for p in v)):
            raise ErrorReceta("«partes» requiere una lista de ternas de enteros.")
        if k in CAPAS and not lista_int(v, 2):
            raise ErrorReceta("«%s» requiere dos enteros (modelo y parte)." % k)
        if k == "pelo_r17" and (not isinstance(v, str) or len(v) > 48 or re.fullmatch(r"[a-z]+(?:_[a-z]+)+", v) is None):
            raise ErrorReceta("ID de peinado R17 inválido.")
        if (k == "ocultar" or k.endswith("_quitar")) and (not isinstance(v, list) or any(type(x) is not int for x in v)):
            raise ErrorReceta("«%s» requiere una lista de enteros." % k)
        if k not in CADENAS and k not in TERNAS and k not in CAPAS and k not in ("partes", "xv2", "ocultar") and not k.endswith("_quitar") and type(v) is not int:
            raise ErrorReceta("«%s» requiere un entero." % k)
        if k not in ORDEN and k not in CADENAS and type(v) is not int:
            raise ErrorReceta("Clave de receta desconocida: «%s»." % k)
        try:
            campos.append("%s=%s" % (k, val(k, v)))
        except (TypeError, ValueError, AttributeError) as e:
            raise ErrorReceta("La receta tiene un valor roto en «%s»." % k) from e
    return ";".join(campos)


def canonica(txt):
    """Texto de ficha en el orden canonico (el de heroe.cpp): dos fichas iguales dan el mismo texto."""
    return _dict_a_ficha(_ficha_a_dict(txt))


# ---------------------------------------------------------------- traje_cac.toml
def _toml_val(v):
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, int):
        return str(v)
    if isinstance(v, list):
        return "[" + ", ".join(_toml_val(x) for x in v) + "]"
    if isinstance(v, dict):
        return "{ " + ", ".join('%s = %s' % (_toml_clave(k), _toml_val(x)) for k, x in v.items()) + " }"
    s = str(v).replace("\\", "\\\\").replace('"', '\\"')
    s = "".join(c if c >= " " and c != "\x7f" else "\\u%04x" % ord(c) for c in s)
    return '"%s"' % s


def _toml_clave(k):
    return k if k.replace("_", "").isalnum() else _toml_val(k)


def a_receta(ficha_txt):
    """Texto --ficha -> texto de traje_cac.toml."""
    d = _ficha_a_dict(ficha_txt)
    if "cuerpo" not in d:
        raise ErrorReceta("La ficha no dice de qué cuerpo parte.")
    claves = [k for k in ORDEN if k in d] + [k for k in d if k not in ORDEN]
    out = ["# Receta de traje del creador (sin arte): el modelo se monta con el juego de quien la abre.",
           "[traje_cac]", "version = %d" % VERSION]
    out += ["%s = %s" % (_toml_clave(k), _toml_val(d[k])) for k in claves]
    return "\n".join(out) + "\n"


def de_receta(toml_txt):
    """Texto de traje_cac.toml -> texto --ficha. Otra version o un archivo roto: ErrorReceta."""
    try:
        t = tomllib.loads(toml_txt).get("traje_cac")
    except tomllib.TOMLDecodeError:
        raise ErrorReceta("El archivo de la receta está roto.")
    if not isinstance(t, dict):
        raise ErrorReceta("El archivo no es una receta de traje.")
    if type(t.get("version")) is not int or t.get("version") != VERSION:
        raise ErrorReceta("La receta es de otra versión (%s); este juego entiende la %d." % (t.get("version"), VERSION))
    d = {k: v for k, v in t.items() if k != "version"}
    if "cuerpo" not in d:
        raise ErrorReceta("La receta no dice de qué cuerpo parte.")
    txt = _dict_a_ficha(d)
    _ficha_a_dict(txt)   # valida tipos
    return txt


def leer(ruta):
    with open(ruta, "r", encoding="utf-8") as fh:
        return de_receta(fh.read())


def escribir(ruta, ficha_txt):
    with open(ruta, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(a_receta(ficha_txt))


# ---------------------------------------------------------------- codigo para compartir (CAC-20)
# Solo campos de apariencia consumidos por heroe.cpp::AplicarFicha.
# Compatibilidad con codigos v1 antiguos: se aceptan, pero se descartan los metadatos.
_PORTATILES = (set(ORDEN) - {"nombre", "cv", "ut", "aura_col", "ki_col", "razas_b3",
                               "material17", "pintura17", "foto_pose", "foto_yaw"}
               - {f"forma{i}" for i in range(1, 8)} - {f"estallido{i}" for i in range(1, 8)})


def _ficha_portatil(d):
    txt = _dict_a_ficha({k: v for k, v in d.items() if k in _PORTATILES})
    if not re.fullmatch(r"[A-Za-z0-9_,;:=|+.-]*", txt):
        raise ErrorReceta("La receta tiene caracteres no válidos.", "roto")
    return txt


def a_codigo(ficha_txt):
    """DBZ3R1.<base64 url de zlib(ficha canonica)>-<crc32 en hex>. Solo la receta: nada de arte."""
    raw = _ficha_portatil(_ficha_a_dict(ficha_txt)).encode("utf-8")
    if len(raw) > MAX_FICHA:
        raise ErrorReceta("La receta es demasiado grande para compartirla.")
    return "%s.%s.%08x" % (PREFIJO, base64.urlsafe_b64encode(zlib.compress(raw, 9)).decode("ascii").rstrip("="),
                           binascii.crc32(raw))


def de_codigo(codigo):
    """Codigo -> texto --ficha canonico, o ErrorReceta con un mensaje claro (el codigo roto no se guarda)."""
    c = "".join(str(codigo).split())
    # zlib + base64 de MAX_FICHA no llega a 6 KiB, aun sin compresion.
    if len(c) > 6144:
        raise ErrorReceta("La receta es demasiado grande para compartirla.", "grande")
    partes = c.split(".")
    if len(partes) != 3 or not partes[0].startswith("DBZ3R"):
        raise ErrorReceta("Eso no es un código de guerrero de DBZ3 HD.", "no_codigo")
    if partes[0] != PREFIJO:
        raise ErrorReceta("El código es de otra versión (%s); este juego entiende la %s." % (partes[0][5:], PREFIJO[5:]), "otra_version")
    try:
        if not re.fullmatch(r"[A-Za-z0-9_-]+", partes[1]) or not re.fullmatch(r"[A-Fa-f0-9]{8}", partes[2]):
            raise ValueError
        comp = base64.urlsafe_b64decode(partes[1] + "=" * (-len(partes[1]) % 4))
        dec = zlib.decompressobj()
        raw = dec.decompress(comp, MAX_FICHA + 1)   # tope: un zip-bomba no llena la memoria
        if len(raw) > MAX_FICHA or dec.unconsumed_tail:
            raise ErrorReceta("La receta es demasiado grande para compartirla.", "grande")
        if not dec.eof or dec.unused_data:
            raise ValueError
        if int(partes[2], 16) != binascii.crc32(raw):
            raise ErrorReceta("El código está incompleto o mal copiado (la suma de control no cuadra).", "crc")
        txt = raw.decode("utf-8")
    except ErrorReceta:
        raise
    except (ValueError, binascii.Error, zlib.error):
        raise ErrorReceta("El código está incompleto o mal copiado.")
    d = _ficha_a_dict(txt)
    if "cuerpo" not in d:
        raise ErrorReceta("El código no lleva un cuerpo: no se puede montar.", "sin_cuerpo")
    return _ficha_portatil(d)


# ---------------------------------------------------------------- CAC-09: traje extra de un luchador nativo
HERE = os.path.dirname(os.path.abspath(__file__))
PREF_MOD = "cac_traje_"      # mods/cac_traje_<nombre>/ (un nivel: roster_build y src/mods.cpp ya buscan ahi su traje.toml)


def _slug(nombre):
    import unicodedata  # noqa: PLC0415
    s = unicodedata.normalize("NFKD", str(nombre)).encode("ascii", "ignore").decode().lower()
    return re.sub(r"[^a-z0-9]+", "_", s).strip("_")[:24] or "traje"


def _huella(ficha_txt):
    ficha = canonica(ficha_txt)
    if any(p in ("0", "1", "2", "pelo", "torso", "armadura") for p in re.findall(r"(?:^|;)capa[1-4]=[0-9]+:([^;]+)", ficha)):
        ficha = "cac1-capas-v2:" + ficha  # invalida bins anteriores con capas de sustitución
    return hashlib.sha256(ficha.encode("utf-8")).hexdigest()[:16]


def _vh():
    """vista_heroe.py como libreria (monta el modelo de la ficha con el juego de quien la abre)."""
    sys.path.insert(0, HERE)
    sys.path.insert(0, os.path.dirname(HERE))
    import vista_heroe  # noqa: PLC0415
    return vista_heroe


def montar_bins(juego, mod_dir, us=None):
    """Monta modelos/*.bin y traje.toml de mod_dir (que ya tiene traje_cac.toml) con el juego de quien lo hace. Un modelo por
    forma del luchador: el de la receta donde la receta tiene forma (base y SSJ de su estilo) y el del juego, tal cual, en las
    demas (Kaioken, SSJ4...). Nada de los bins nativos cambia: solo se anade un traje al final."""
    import tempfile  # noqa: PLC0415
    ficha_txt = leer(os.path.join(mod_dir, "traje_cac.toml"))
    vh = _vh()
    rb = vh.rb
    f = vh.leer_ficha(ficha_txt)
    cuerpo, traje = int(f.get("cuerpo", 0)), max(int(f.get("traje", 0)), 0)
    fids = vh.KFID.get(cuerpo)
    row = rb.IDS.get(cuerpo) or {}
    if not fids or not row.get("models"):
        raise ErrorReceta("Este cuerpo no admite un traje extra: solo los luchadores del juego.")
    f["donante"] = cuerpo           # el moveset y las transformaciones son los del luchador
    work = tempfile.mkdtemp(prefix="recetas_")
    afs = vh.abrir_afs(us, work, juego)
    fid = fids[min(traje, len(fids) - 1)]
    datos, r = vh.combate_bin(afs.entry(fid), f, juego, afs)
    formas = vh.formas_ssj(afs, fid, f, juego, r) or []
    vent, tope, excedido = vh.tope_info(r)   # CAC-13: una mezcla que no cabe nunca deja un mod roto ni con zonas en Base
    if excedido:
        raise ErrorReceta("La mezcla pasa del tope de %d ventanas (%d). No se guarda." % (tope, vent))
    fm = row.get("form_models") or [[cuerpo, 0]]
    out = os.path.join(mod_dir, "modelos")
    os.makedirs(out, exist_ok=True)
    nombres, bins = {0: "base.bin"}, {0: datos}
    for d, forma in formas:
        nombres[forma] = "forma%d.bin" % forma
        bins[forma] = d
    modelos = []
    for i, (donante, forma) in enumerate(fm):
        if i in bins:
            with open(os.path.join(out, nombres[i]), "wb") as fh:
                fh.write(bins[i])
            modelos.append("modelos/" + nombres[i])
            continue
        dr = rb.IDS.get(donante) or rb.DB.get("form_ids", {}).get(str(donante)) or row   # la forma de otro ID (Goku SSJ4 = 91)
        mpc = int(dr.get("models_per_costume") or 1)
        lista = dr.get("models") or []
        costume = min(traje, max(1, len(lista) // mpc) - 1)
        modelos.append("juego:%d" % lista[min(costume * mpc + min(forma, mpc - 1), len(lista) - 1)])
    nombre = str(f.get("nombre") or "Traje").replace('"', "").replace("\\", "")[:24]
    with open(os.path.join(mod_dir, "traje.toml"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write("# Generado por recetas.py (Guardar como traje). El arte se monta con el juego de quien lo abre.\n"
                 "[traje]\npersonaje = %d\nnombre = \"%s\"\nreceta = \"traje_cac.toml\"\nmodelos = [%s]\n"
                 % (cuerpo, nombre, ", ".join('"%s"' % m for m in modelos)))
    with open(os.path.join(out, ".receta"), "w", encoding="utf-8") as fh:
        fh.write(_huella(ficha_txt))
    return {"formas": len(modelos), "propias": sorted(bins), "modelos": modelos}


def montaje_vigente(mod_dir):
    """True si los bins de mod_dir ya son los de su receta (roster_build solo remonta si no)."""
    try:
        return open(os.path.join(mod_dir, "modelos", ".receta"), encoding="utf-8").read() == _huella(leer(os.path.join(mod_dir, "traje_cac.toml")))
    except (OSError, ErrorReceta):
        return False


def guardar_traje(juego, ficha_txt, nombre, us=None):
    """«Guardar como traje»: mods/cac_traje_<nombre>/ (receta + traje.toml + bins). Una misma receta no se duplica."""
    mods = os.path.join(juego, "mods")
    base = PREF_MOD + _slug(nombre)
    d, k = os.path.join(mods, base), 2
    while os.path.exists(d):
        try:
            if _huella(leer(os.path.join(d, "traje_cac.toml"))) == _huella(ficha_txt):
                break              # la misma receta con el mismo nombre: se rehace en su sitio
        except (OSError, ErrorReceta):
            pass
        d, k = os.path.join(mods, "%s_%d" % (base, k)), k + 1
    nuevo = not os.path.exists(d)
    os.makedirs(d, exist_ok=True)
    f = _ficha_a_dict(ficha_txt)
    f.pop("donante", None)
    f["nombre"] = nombre
    escribir(os.path.join(d, "traje_cac.toml"), _dict_a_ficha(f))
    try:
        r = montar_bins(juego, d, us)
    except Exception:
        if nuevo:   # CAC-13: una receta que no se monta (tope...) no deja una carpeta a medias (solo lo recien creado aqui)
            import shutil  # noqa: PLC0415
            shutil.rmtree(d, ignore_errors=True)
        raise
    r.update(carpeta=d, mod=os.path.basename(d))
    return r


# ---------------------------------------------------------------- CAC-19: una forma del heroe = otra receta
# La ficha del heroe lleva `formaN=<hex de la receta>` (N = forma del donante, 1..7) y, opcional, `estallidoN=#RRGGBB` (color del
# estallido de esa forma). vista_heroe.py debe dejar pasar esas claves como cadenas (leer_ficha) y llamar, tras montar el mod
# del heroe, a recetas.aplicar_formas(juego, mod_dir, ficha_dict): cada forma pedida sustituye su modelo por el de su receta.
def formas_de_ficha(f):
    """{forma: (texto --ficha de la receta, color de estallido o None)} de una ficha ya leida (dict de leer_ficha)."""
    out = {}
    for k, v in f.items():
        m = re.fullmatch(r"forma([1-7])", k)
        if not m or not str(v).strip():
            continue
        try:
            if len(str(v)) > 2 * MAX_FICHA:
                continue
            txt = bytes.fromhex(str(v)).decode("utf-8")
            if "cuerpo" not in _ficha_a_dict(txt):
                continue
        except (ValueError, ErrorReceta):
            continue                      # una forma con la receta rota se queda con la nativa: nunca rompe el montaje
        color = f.get("estallido%s" % m.group(1))
        out[int(m.group(1))] = (txt, color if isinstance(color, str) and re.fullmatch(r"#[0-9A-Fa-f]{6}", color) else None)
    return out


def hex_de_ficha(txt):
    return canonica(txt).encode("utf-8").hex()


def modelo_receta_forma(juego, ficha, afs, forma, txt):
    """Mismo montaje para vista y publicación. None deja el modelo que ya tenía la forma."""
    vh = _vh()
    g = vh.leer_ficha(txt)
    cuerpo = ficha.get("cuerpo", 0)
    if g.get("cuerpo") != cuerpo:
        return None
    g["donante"] = ficha.get("donante", cuerpo)
    if vh.XV2_CUERPO[0] <= cuerpo <= vh.XV2_CUERPO[1]:
        datos, r = vh.modelo_xv2(afs, g, juego, forma=forma)
    else:
        fids = vh.KFID.get(cuerpo)
        if not fids:
            return None
        fid = fids[min(max(g.get("traje", 0), 0), len(fids) - 1)]
        datos, r = vh.combate_bin(afs.entry(fid), g, juego, afs)
        datos = dict((fm, d) for d, fm in vh.formas_ssj(afs, fid, g, juego, r)).get(forma, datos)
    return None if vh.tope_info(r)[2] else (datos, r)


def aplicar_formas(juego, mod_dir, ficha, afs, modelos, mf):
    """Reemplaza solo la forma pedida; nunca modifica un bin compartido ni desbloquea formas."""
    pedidas = formas_de_ficha(ficha)
    if not pedidas:
        return {}
    hechas = {}
    for forma, (txt, _estallido) in sorted(pedidas.items()):
        if not 0 < forma < len(mf):
            continue
        try:
            montaje = modelo_receta_forma(juego, ficha, afs, forma, txt)
            if montaje is None:
                continue
            datos, _ = montaje
        except (ValueError, KeyError, IndexError):
            continue  # conservar la forma que ya había
        rel = "modelos/receta_forma%d.bin" % forma
        # Reutilizar una entrada solo si nadie mas (incluida la base) la usa.
        idx = mf[forma]
        separado = mf.count(idx) > 1
        if separado and len(modelos) >= 8:   # límite de roster_build
            continue
        dst = os.path.join(mod_dir, rel)
        try:
            with open(dst + ".tmp", "wb") as fh:
                fh.write(datos)
            os.replace(dst + ".tmp", dst)
        except OSError as e:
            print("No se pudo montar la receta de forma %d: %s" % (forma, e))
            continue
        if separado:
            mf[forma] = len(modelos)
            modelos.append(rel)
        else:
            modelos[idx] = rel
        hechas[forma] = rel
    return hechas


# ---------------------------------------------------------------- CLI para el juego (GenerarCon: --juego J ...)
def _json(juego, datos, peticion=None, salida=None):
    d = salida or os.path.join(juego, "ingame_ui", "heroe_vista")
    os.makedirs(d, exist_ok=True)
    nombre = peticion + ".json" if peticion else "recetas.json"
    dst = os.path.join(d, nombre)
    with open(dst + ".tmp", "w", encoding="utf-8") as fh:
        json.dump(datos, fh, ensure_ascii=False)
    os.replace(dst + ".tmp", dst)


def cli_juego(a):
    """--juego J --guardar-traje "<ficha>" --nombre N | --codigo "<ficha>" | --decodificar "<codigo>": deja
    ingame_ui/heroe_vista/recetas.json = {estado: ok|error|fallo, ...} (lo lee heroe_ui.cpp)."""
    import argparse  # noqa: PLC0415
    ap = argparse.ArgumentParser()
    ap.add_argument("--juego", required=True)
    ap.add_argument("--us")
    ap.add_argument("--salida")                      # lo anade GenerarCon para vista_heroe.py; aqui no se usa
    ap.add_argument("--nombre", default="Traje")
    ap.add_argument("--guardar-traje", dest="guardar")
    ap.add_argument("--codigo")
    ap.add_argument("--decodificar")
    ap.add_argument("--peticion")  # cada accion C++ tiene su propio resultado
    ap.add_argument("--montar")                      # carpeta de un traje con receta: rehace sus bins (roster_build)
    x = ap.parse_args(a)
    if x.peticion and not re.fullmatch(r"codigo_[0-9]+_[0-9]+", x.peticion):
        ap.error("peticion no válida")
    try:
        if x.montar:
            r = montar_bins(x.juego, x.montar, x.us)
            _json(x.juego, {"estado": "ok", "mod": os.path.basename(x.montar), "formas": r["formas"]})
        elif x.guardar:
            r = guardar_traje(x.juego, x.guardar, x.nombre, x.us)
            _json(x.juego, {"estado": "ok", "mod": r["mod"], "formas": r["formas"]})
        elif x.codigo:
            _json(x.juego, {"estado": "ok", "codigo": a_codigo(x.codigo)}, x.peticion, x.salida)
        elif x.decodificar is not None:
            _json(x.juego, {"estado": "ok", "ficha": de_codigo(x.decodificar)}, x.peticion, x.salida)
        else:
            return 2
    except ErrorReceta as e:
        _json(x.juego, {"estado": "error", "clave": e.clave, "mensaje": str(e)}, x.peticion, x.salida)
        return 0 if x.peticion else 1  # resultado de dominio; el proceso si termino bien
    except Exception as e:  # noqa: BLE001 (el jugador ve un aviso; el detalle va al registro)
        import traceback  # noqa: PLC0415
        traceback.print_exc()
        _json(x.juego, {"estado": "fallo", "mensaje": "%s: %s" % (type(e).__name__, e)}, x.peticion, x.salida)
        return 1
    return 0


# ---------------------------------------------------------------- autoprueba
def _al_azar(rng):
    """Una ficha como la que escribe heroe.cpp ArgsFicha (mismo orden), con valores al azar."""
    hsv = lambda: "%d,%d,%d" % (rng.randint(0, 360), rng.randint(-100, 100), rng.randint(-100, 100))
    p = ["cuerpo=%d" % rng.choice([0, 1, 2, 3, 100, 105]), "traje=%d" % rng.randint(0, 6),
         "partes=" + "|".join(hsv() for _ in range(rng.choice([4, 6])))]
    if rng.random() < .3:
        p.append("xv2=bust:%d,pants:%d,boots:%d,rist:%d,cara:%d,pelo:%d,ojos:%d" % tuple(rng.randint(1, 9) for _ in range(7)))
        p.append("cv=%08x" % rng.getrandbits(32))
    if rng.random() < .5:
        p.append("rostro=%d" % rng.randint(1, 13))
    for k in ("ojos", "piel"):
        if rng.random() < .4:
            p.append("%s=%s" % (k, hsv()))
    p += ["pelo=%d" % rng.randint(0, 10), "c1=" + hsv(), "c2=" + hsv(), "logo=%d" % rng.randint(0, 5), "scouter=%d" % rng.randint(0, 1)]
    for z in ("torso", "piernas", "guantes", "botas"):
        if rng.random() < .5:
            p.append("b3_%s=%d" % (z, rng.randint(0, 4000)))
    if rng.random() < .4:
        p.append("b3_accesorios=%d" % rng.randint(0, 4000))
    for i in range(1, 5):
        if rng.random() < .4:
            p.append("capa%d=%d:%d" % (i, rng.randint(0, 4000), rng.randint(0, 3)))   # vista_heroe CAPA_PARTES: pelo, torso, armadura, accesorios
            if rng.random() < .5:
                p.append("capa%d_quitar=%s" % (i, ",".join(str(x) for x in sorted(rng.sample(range(40), 3)))))
    if rng.random() < .4:   # CAC-17: colores por entrada de textura
        p.append("texcol=" + "|".join("%d,%d,%s" % (rng.randint(0, 30), rng.randint(0, 5), hsv()) for _ in range(rng.randint(1, 3))))
    if rng.random() < .3:
        p.append("ropa=%d" % rng.randint(1, 20))
    if rng.random() < .3:
        p += ["aura_col=#%06X" % rng.getrandbits(24), "ki_col=#%06X" % rng.getrandbits(24)]
    if rng.random() < .3:
        p.append("ut=4")
    if rng.random() < .3:
        p.append("razas_b3=%d" % rng.choice([1, 3]))
    if rng.random() < .5:
        p.append("orig%d=%d" % (rng.randint(1, 4), rng.randint(0, 3999)))
    if rng.random() < .5:
        p.append("ocultar=" + ",".join(str(x) for x in sorted(rng.sample(range(64), rng.randint(1, 8)))))
    p.append("nombre=" + rng.choice(["Heroe", "Kakaroto", "Ñandú Z", "Trunks 2"]))
    return ";".join(p)


def autoprueba(n=10, semilla=8):
    rng = random.Random(semilla)
    for i in range(n):
        f = _al_azar(rng)
        assert canonica(f) == f, ("orden canonico distinto del de heroe.cpp", f, canonica(f))
        via_toml = de_receta(a_receta(f))
        assert via_toml == f, ("toml", f, via_toml)
        via_codigo = de_codigo(a_codigo(f))
        assert via_codigo == _ficha_portatil(_ficha_a_dict(f)), ("codigo", f, via_codigo)
        # lo que el jugador teclea: espacios y saltos de linea al pegar no rompen el codigo
        cod = a_codigo(f)
        assert de_codigo(cod[:20] + "\n " + cod[20:] + " ") == via_codigo
        # mismo texto = misma entrada del montaje de bins (vista_heroe.leer_ficha) -> mismo bin y mismo hash
        assert _ficha_a_dict(via_codigo) == {k: v for k, v in _ficha_a_dict(f).items() if k in _PORTATILES}
    r1, r2 = _al_azar(rng), _al_azar(rng)   # CAC-19: formaN = hex de la receta; una forma rota se ignora
    fo = formas_de_ficha({"forma2": hex_de_ficha(r1), "estallido2": "#FFAA00", "forma3": hex_de_ficha(r2), "forma5": "zz"})
    assert fo == {2: (r1, "#FFAA00"), 3: (r2, None)}, fo
    for invalido in ("cuerpo=0;texcol=0,6,120,0,0", "cuerpo=0;capa1=390:4"):
        try:
            a_receta(invalido)
        except ErrorReceta:
            pass
        else:
            raise AssertionError("receta inválida aceptada: " + invalido)
    f = _al_azar(random.Random(1))
    cod = a_codigo(f)
    malos = {"roto": cod[:-3] + "000", "basura": "hola", "otra version": cod.replace("DBZ3R1", "DBZ3R9", 1),
             "cortado": cod[:30], "sin cuerpo": a_codigo("traje=1")}
    for nombre, c in malos.items():
        try:
            de_codigo(c)
        except ErrorReceta as e:
            print("   %-13s -> %s" % (nombre, e))
        else:
            raise AssertionError("codigo %s aceptado" % nombre)
    try:
        de_receta('[traje_cac]\nversion = 7\ncuerpo = 1\n')
    except ErrorReceta as e:
        print("   toml v7       -> %s" % e)
    else:
        raise AssertionError("toml de otra version aceptado")
    print("recetas: autoprueba OK (%d recetas al azar: ficha -> toml -> ficha -> codigo -> ficha, texto identico)" % n)


if __name__ == "__main__":
    a = sys.argv[1:]
    try:
        if "--juego" in a:
            sys.exit(cli_juego(a))
        if a[:1] == ["autoprueba"]:
            autoprueba()
        elif a[:1] == ["a-ficha"]:
            print(leer(a[1]))
        elif a[:1] == ["a-receta"]:
            escribir(a[2], a[1]) if len(a) > 2 else print(a_receta(a[1]), end="")
        elif a[:1] == ["codigo"]:
            print(a_codigo(leer(a[1])))
        elif a[:1] == ["decodificar"]:
            f = de_codigo(a[1])
            escribir(a[2], f) if len(a) > 2 else print(f)
        else:
            print(__doc__)
    except ErrorReceta as e:
        print("ERROR: %s" % e)
        sys.exit(1)
