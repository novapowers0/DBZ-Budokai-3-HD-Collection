"""Estado mutable de una copia de pruebas: restauracion verificada, sin borrar datos.

El llamador mantiene copia_en_uso y comprueba que no quede un juego abierto.
Los binarios, assets, logs y capturas quedan fuera; mods (incluidos sus modelos
exportados), partidas, recetas y ajustes vuelven exactamente a su contenido previo.
"""
import hashlib
import json
import os
import shutil
import stat
import time
import zipfile
from contextlib import contextmanager, nullcontext
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime
from pathlib import Path
from uuid import uuid4


RESPALDOS = Path(os.environ.get("DBZ3_AISLAMIENTO_DIR", r"D:\Caches\dbz3_pruebas\aislamiento")) / "ejecuciones"   # D: se lleno (15/11)
CARPETAS = {"mods", "mods_nativos", "user_data", "saves", "save", "exports", "export",
            "exportaciones", "recetas_heroe"}


_LEGACY = Path(r"E:\DBZ3HD_trabajo\aislamiento_pruebas\ejecuciones")
GRANDES = {"mods", "exports", "export", "exportaciones"}   # arboles de cientos de MB que una prueba normal no toca


def _mutable(nombre):
    n = nombre.lower()
    # VEL r23 (coordinador 23:54): por defecto fuera los arboles grandes; DBZ3_AISLAMIENTO_MODS=1 los incluye (guiones que instalan mods)
    if n in GRANDES and os.environ.get("DBZ3_AISLAMIENTO_MODS") != "1":
        return False
    return (n in CARPETAS or n.startswith("dbz3_user.")
            or any(s in (".toml", ".ini", ".cfg", ".sav", ".save") for s in Path(n).suffixes)
            or (n.startswith("dbz3_") and ".req" in Path(n).suffixes))


def _sha(path):
    # Windows puede mantener bloqueado brevemente un archivo recién copiado
    # (medido en R18 al verificar geom.bin). Reintentar la lectura completa;
    # nunca omitir su hash ni dar por bueno un respaldo que no se pudo leer.
    for intento in range(31):
        try:
            h = hashlib.sha256()
            with path.open("rb") as f:
                for bloque in iter(lambda: f.read(1024 * 1024), b""):
                    h.update(bloque)
            return h.hexdigest()
        except PermissionError:
            if intento == 30:
                raise
            time.sleep(0.1)


def _ruta_cache(dest):
    clave = hashlib.sha256(str(Path(dest).resolve()).lower().encode()).hexdigest()[:20]
    return RESPALDOS.parent / "hashes" / (clave + ".json")


def _almacen(sha, respaldos=RESPALDOS):
    return Path(respaldos).parent / "almacen" / sha[:2] / sha


def inventario(dest):
    """Incluye carpetas vacias y hashes reales; nunca sigue enlaces o uniones.
    VEL-03: un archivo con el mismo tamano y la misma fecha (ns) que la ultima vez que se hasheo conserva su sha256
    (cache en aislamiento_pruebas/hashes; no vale si se toco hace < 3 s; DBZ3_AISLAMIENTO_SIN_CACHE=1 lo apaga).
    Antes cada prueba leia 4 veces todos los bytes (148 s medidos con 234 MB de mods)."""
    dest = Path(dest)
    usar_cache = os.environ.get("DBZ3_AISLAMIENTO_SIN_CACHE") != "1"
    cache = {}
    if usar_cache:
        try:
            cache = json.loads(_ruta_cache(dest).read_text(encoding="utf-8"))
        except (OSError, ValueError):
            cache = {}
    ahora_ns = time.time_ns()
    filas = {}
    archivos = []

    def visitar(entrada):
        p = Path(entrada.path)
        # En Windows scandir ya trae estos atributos del listado; lstat por
        # cada ruta vuelve a abrir miles de archivos en discos lentos.
        st = entrada.stat(follow_symlinks=False)
        if stat.S_ISLNK(st.st_mode) or getattr(st, "st_file_attributes", 0) & 0x400:
            raise RuntimeError(f"aislamiento: enlace/union en estado mutable: {p}")
        rel = p.relative_to(dest).as_posix()
        if stat.S_ISDIR(st.st_mode):
            if entrada.name.lower() == "cache":
                return
            filas[rel] = {"tipo": "carpeta"}
            with os.scandir(p) as hijos:
                for hijo in sorted(hijos, key=lambda e: e.name):
                    visitar(hijo)
        elif stat.S_ISREG(st.st_mode):
            archivos.append((rel, p))
            filas[rel] = {"tipo": "archivo", "bytes": st.st_size,
                          "mtime_ns": st.st_mtime_ns}
        else:
            raise RuntimeError(f"aislamiento: archivo especial no admitido: {p}")

    if dest.exists():
        with os.scandir(dest) as entradas:
            for entrada in sorted(entradas, key=lambda e: e.name):
                if _mutable(entrada.name):
                    visitar(entrada)
    # Optativo para discos con mucha latencia al abrir miles de archivos pequenos.
    pendientes = []
    for rel, p in archivos:
        c = cache.get(rel)
        f = filas[rel]
        if c and c[0] == f["bytes"] and c[1] == f["mtime_ns"] and ahora_ns - f["mtime_ns"] > 3_000_000_000:
            f["sha256"] = c[2]
        else:
            pendientes.append((rel, p))
    with ThreadPoolExecutor(max_workers=_trabajadores()) as pool:
        for i, ((rel, _), firma) in enumerate(zip(pendientes, pool.map(_sha, (p for _, p in pendientes))), 1):
            filas[rel]["sha256"] = firma
            if i % 2000 == 0:
                print(f"   aislamiento: hashes {i}/{len(pendientes)}", flush=True)
    if usar_cache and pendientes:
        try:
            _json(_ruta_cache(dest), {rel: [f["bytes"], f["mtime_ns"], f["sha256"]]
                                      for rel, f in filas.items() if f["tipo"] == "archivo"})
        except OSError:
            pass
    return filas


def _trabajadores():
    # 15/11: con 1 hilo el inventario de ~7000 archivos pasaba de 15 min con varias pruebas a la vez; 8 hilos: ~30 s
    return max(1, min(8, int(os.environ.get("DBZ3_AISLAMIENTO_HILOS", "8"))))


def _json(path, dato):
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + "." + uuid4().hex + ".tmp")
    tmp.write_text(json.dumps(dato, ensure_ascii=False, sort_keys=True, indent=2) + "\n", encoding="utf-8")
    os.replace(tmp, path)


class Estado:
    def __init__(self, dest, respaldos=RESPALDOS):
        self.dest = Path(dest).resolve()
        self.respaldos = Path(respaldos).resolve()
        clave = hashlib.sha256(str(self.dest).lower().encode()).hexdigest()[:20]
        self.pendiente = self.respaldos / "_pendientes" / (clave + ".json")
        self.carpeta = None
        self.antes = None

    def recuperar(self):
        """Una muerte del ejecutor deja recibo: se restaura antes de la siguiente prueba."""
        if not self.pendiente.exists():
            # recibos del banco anterior (E:, USB lento): se restauran desde su ruta y no se vuelven a usar
            viejo = _LEGACY / "_pendientes" / self.pendiente.name
            if self.respaldos == _LEGACY.resolve() or not viejo.exists():
                return
            sano = self.respaldos, self.pendiente
            self.respaldos, self.pendiente = _LEGACY.resolve(), viejo
            try:
                self._recuperar()
            finally:
                self.respaldos, self.pendiente = sano
            return
        self._recuperar()

    def _recuperar(self):
        recibo = json.loads(self.pendiente.read_text(encoding="utf-8"))
        carpeta = Path(recibo["respaldo"]).resolve()
        if recibo["destino"] != str(self.dest) or not carpeta.is_relative_to(self.respaldos):
            raise RuntimeError("aislamiento: recibo pendiente no corresponde a esta copia")
        self.carpeta = carpeta
        manifest = json.loads((carpeta / "antes.json").read_text(encoding="utf-8"))
        if manifest["destino"] != str(self.dest):
            raise RuntimeError("aislamiento: manifiesto de otra copia")
        # respaldos hechos con otro alcance (p. ej. con mods/): solo se compara lo que el inventario actual cubre
        self.antes = {r: d for r, d in manifest["archivos"].items()
                      if _mutable(r.split("/")[0]) and "cache" not in r.lower().split("/")}
        print(f"   aislamiento: recuperando prueba interrumpida: {carpeta}", flush=True)
        self.restaurar()

    def guardar(self):
        print(f"   aislamiento: inventariando {self.dest}", flush=True)
        self.antes = inventario(self.dest)
        nombre = f"{datetime.now():%Y%m%d-%H%M%S}_{self.dest.name}_{uuid4().hex[:8]}"
        self.carpeta = self.respaldos / nombre
        original = self.carpeta / "original"
        original.mkdir(parents=True)
        if os.environ.get("DBZ3_AISLAMIENTO_ARCHIVO") == "1":
            # Un solo contenedor evita miles de aperturas en el disco de respaldo.
            # El recibo se publica solo tras verificar TODOS los bytes archivados.
            archivo = self.carpeta / "original.zip"
            with zipfile.ZipFile(archivo, "w", compression=zipfile.ZIP_STORED, allowZip64=True) as z:
                for rel, dato in self.antes.items():
                    if dato["tipo"] == "archivo":
                        z.write(self.dest / rel, rel)
            with zipfile.ZipFile(archivo) as z:
                for rel, dato in self.antes.items():
                    if dato["tipo"] == "archivo":
                        with z.open(rel) as f:
                            firma = hashlib.file_digest(f, "sha256").hexdigest()
                        if firma != dato["sha256"]:
                            raise RuntimeError(f"aislamiento: archivo cambio mientras se archivaba: {rel}")
            _json(self.carpeta / "antes.json", {"destino": str(self.dest), "archivos": self.antes})
            _json(self.pendiente, {"destino": str(self.dest), "respaldo": str(self.carpeta)})
            print(f"   aislamiento: {len(self.antes)} rutas respaldadas y verificadas en {archivo}", flush=True)
            return
        # VEL-03 (r23): almacen por contenido (sha256). Un archivo ya guardado en una corrida anterior no se copia
        # otra vez; solo se copia lo nuevo o cambiado. Antes cada prueba copiaba 234-777 MB de mods.
        archivos = [(rel, dato) for rel, dato in self.antes.items() if dato["tipo"] == "archivo"]
        copiados = []
        def copiar(item):
            rel, dato = item
            dst = _almacen(dato["sha256"], self.respaldos)
            try:
                if dst.stat().st_size == dato["bytes"]:
                    return
            except OSError:
                pass
            dst.parent.mkdir(parents=True, exist_ok=True)
            tmp = dst.with_name(dst.name + "." + uuid4().hex[:8] + ".tmp")
            shutil.copyfile(self.dest / rel, tmp)
            if _sha(tmp) != dato["sha256"]:
                raise RuntimeError(f"aislamiento: archivo cambio mientras se copiaba: {rel}")
            os.replace(tmp, dst)
            copiados.append(dato["bytes"])
        with ThreadPoolExecutor(max_workers=_trabajadores()) as pool:
            for _ in pool.map(copiar, archivos):
                pass
        _json(self.carpeta / "antes.json", {"destino": str(self.dest), "archivos": self.antes})
        # Solo se publica cuando el respaldo esta completo; aun no se ha cambiado la copia.
        _json(self.pendiente, {"destino": str(self.dest), "respaldo": str(self.carpeta)})
        print(f"   aislamiento: {len(archivos)} archivos en el almacen; copiados ahora {len(copiados)} ({sum(copiados) / 1024**2:.1f} MB)", flush=True)

    def restaurar(self):
        actual = inventario(self.dest)
        # Apartar solo lo nuevo o cambiado, empezando por el padre. Nunca unlink/rmtree.
        apartados = []
        padres_apartados = set()
        despues = self.carpeta / ("despues_" + uuid4().hex[:8])
        for rel in sorted(actual, key=lambda r: (len(Path(r).parts), r)):
            if actual[rel] == self.antes.get(rel):
                continue
            if any(p.as_posix() in padres_apartados for p in Path(rel).parents):
                continue
            # Una carpeta que ya existia no se mueve entera: sus hijos se comparan abajo.
            if actual[rel]["tipo"] == "carpeta" and self.antes.get(rel, {}).get("tipo") == "carpeta":
                continue
            salida = despues / rel
            salida.parent.mkdir(parents=True, exist_ok=True)
            shutil.move(str(self.dest / rel), str(salida))
            apartados.append(rel)
            padres_apartados.add(rel)
        archivo = self.carpeta / "original.zip"
        # Leer una sola vez el directorio del ZIP, no una vez por archivo.
        with zipfile.ZipFile(archivo) if archivo.exists() else nullcontext() as z:
            for rel, dato in sorted(self.antes.items(), key=lambda item: (len(Path(item[0]).parts), item[0])):
                # El inventario completo acaba de comprobar estas rutas. Sus
                # padres existentes no se apartan; no reabrir cada ruta intacta.
                if dato == actual.get(rel):
                    continue
                p = self.dest / rel
                if dato["tipo"] == "carpeta":
                    p.mkdir(parents=True, exist_ok=True)
                else:
                    src = _almacen(dato["sha256"], self.respaldos)
                    if not src.exists():
                        src = self.carpeta / "original" / rel      # respaldos anteriores a VEL-03
                    if z is not None:
                        # Nunca extractall: los destinos proceden del inventario validado.
                        with z.open(rel) as f:
                            if hashlib.file_digest(f, "sha256").hexdigest() != dato["sha256"]:
                                raise RuntimeError(f"aislamiento: respaldo dañado: {rel}")
                        p.parent.mkdir(parents=True, exist_ok=True)
                        with z.open(rel) as f, p.open("wb") as salida:
                            shutil.copyfileobj(f, salida)
                        os.utime(p, ns=(dato["mtime_ns"], dato["mtime_ns"]))
                    else:
                        if _sha(src) != dato["sha256"]:
                            raise RuntimeError(f"aislamiento: respaldo dañado; se conserva pendiente: {src}")
                        p.parent.mkdir(parents=True, exist_ok=True)
                        shutil.copyfile(src, p)
                        os.utime(p, ns=(dato["mtime_ns"], dato["mtime_ns"]))   # el almacen no guarda la fecha
        verificado = inventario(self.dest)
        if verificado != self.antes:
            distintos = sorted(k for k in set(verificado) | set(self.antes) if verificado.get(k) != self.antes.get(k))
            raise RuntimeError(f"aislamiento: restauracion incompleta: {distintos[:10]}")
        _json(self.carpeta / "restauracion.json", {"ok": True, "destino": str(self.dest),
              "archivos": len(verificado), "apartados": apartados, "conservados_en": str(despues)})
        shutil.move(str(self.pendiente), str(self.carpeta / ("recibo_restaurado_" + uuid4().hex[:8] + ".json")))
        print(f"   aislamiento: RESTAURADO y verificado ({len(apartados)} rutas apartadas)", flush=True)
        if os.environ.get("DBZ3_AISLAMIENTO_PODAR") == "1":   # VEL r23: borrar respaldos viejos dentro de cada prueba tardaba minutos
            podar(self.respaldos)


def podar(respaldos, conservar=None):
    """Usuario 16/11: 609 respaldos de ~300 MB llenaron E. Solo se borran los ya RESTAURADOS y verificados
    (tienen restauracion.json) y sin recibo pendiente; se conservan los N mas recientes (DBZ3_AISLAMIENTO_CONSERVAR, 20)."""
    conservar = int(os.environ.get("DBZ3_AISLAMIENTO_CONSERVAR", "20")) if conservar is None else conservar
    respaldos = Path(respaldos)
    vivos = set()
    for r in (respaldos / "_pendientes").glob("*.json"):
        try:
            vivos.add(Path(json.loads(r.read_text(encoding="utf-8"))["respaldo"]).name)
        except (OSError, ValueError, KeyError):
            return                                   # recibo ilegible: no arriesgar
    hechos = sorted((d for d in respaldos.iterdir() if d.is_dir() and d.name not in vivos and not d.name.startswith("_")
                     and (d / "restauracion.json").exists()), key=lambda d: d.name)
    for d in hechos[:max(0, len(hechos) - conservar)]:
        shutil.rmtree(d, ignore_errors=True)


@contextmanager
def aislar(dest, comprobar_libre, respaldos=RESPALDOS):
    """Rodea preparacion, cache, escenario y cierre, dentro del candado de la copia."""
    comprobar_libre(dest)
    estado = Estado(dest, respaldos)
    estado.recuperar()
    estado.guardar()
    try:
        yield estado
    finally:
        # Si el juego no consiguio cerrarse, no competir con sus escrituras. Se recuperara mas tarde.
        comprobar_libre(dest)
        estado.restaurar()


if __name__ == "__main__":      # python tools/pruebas_aislamiento.py   -> poda a mano los respaldos ya restaurados
    podar(RESPALDOS)
