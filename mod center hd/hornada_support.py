"""Propuesta portable para el Kit: publicar árboles con respaldo y diario.

No se ejecuta al importar. Integrar junto a roster_build únicamente después de
revisar el diff. El llamador debe parar lectores/guest durante la publicación.
Los dos rename NO son una transacción atómica; el diario identifica el intervalo.
"""
from __future__ import annotations

from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import uuid


def _firma(st):
    """Lo que tiene que seguir igual para fiarse de una huella guardada."""
    return [st.st_size, st.st_mtime_ns, st.st_ino]


def _sha256(p, firmas, rel):
    h = hashlib.sha256()
    with p.open("rb") as stream:
        if firmas is not None:
            firmas[rel] = _firma(os.fstat(stream.fileno()))   # antes de leer
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def fingerprint(root, reuse=None, firmas=None):
    """{ruta: sha256}. `reuse` = {ruta: [tamano, mtime_ns, ino, sha256]} de un montaje anterior:
    solo se reutiliza si las tres primeras siguen iguales; si no, se lee el archivo. `firmas` (dict)
    recibe la firma de cada archivo leido. Lee en varios hilos: casi todo es esperar al disco."""
    root = Path(root)
    if root.is_symlink():
        raise ValueError("No publicar enlaces")
    out, todo = {}, []
    for p in sorted(root.rglob("*")):
        if p.is_symlink() or (hasattr(p, "is_junction") and p.is_junction()):
            raise ValueError("No publicar enlaces/junctions")
        if p.is_file():
            rel = p.relative_to(root).as_posix()
            old = reuse.get(rel) if reuse else None
            out[rel] = old[3] if old and old[:3] == _firma(p.stat()) else None
            if out[rel] is None:
                todo.append((p, rel))
    with ThreadPoolExecutor(8) as pool:
        for (p, rel), h in zip(todo, pool.map(lambda t: _sha256(t[0], firmas, t[1]), todo)):
            out[rel] = h
    return out


def _leer_huellas(path, target):
    """Indice de huellas del ultimo publicado, o None si falta, esta roto o es de otro destino."""
    try:
        doc = json.loads(Path(path).read_text(encoding="utf-8"))
        if doc.get("target") != str(target) or not isinstance(doc.get("files"), dict):
            return None
        return {k: v for k, v in doc["files"].items()
                if isinstance(v, list) and len(v) == 4 and all(isinstance(n, int) for n in v[:3])
                and isinstance(v[3], str) and len(v[3]) == 64}
    except (OSError, ValueError, AttributeError):
        return None


def _guardar_huellas(path, target, hashes, firmas):
    """Guarda {ruta: firma + huella} del arbol recien publicado (fuera de el: no cambia el _roster).
    Si un archivo cambio desde que se leyo (firma distinta), no entra: la proxima vez se relee."""
    files = {}
    for rel, h in hashes.items():
        try:
            st = (Path(target) / rel).stat()
        except OSError:
            continue
        if firmas.get(rel) == _firma(st):
            files[rel] = _firma(st) + [h]
    tmp = Path("%s.%d.tmp" % (path, os.getpid()))
    try:
        tmp.write_text(json.dumps(dict(target=str(target), files=files)), encoding="utf-8")
        os.replace(tmp, path)
    except OSError:
        pass   # sin indice: el proximo montaje calcula las huellas como antes


def _leer_owner(lock):
    """Dueño del lock, o None si no existe o no se puede leer (corte entre mkdir y owner.json)."""
    try:
        owner = json.loads((Path(lock) / "owner.json").read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    return owner if isinstance(owner, dict) else None


def _apartar_lock(lock, store, name):
    """Mueve un lock inservible a mods_respaldo con nombre único. Nunca se borra."""
    destino = Path(store) / ("%s.lock.huerfano-%s" % (name, uuid.uuid4().hex[:8]))
    os.rename(lock, destino)
    print("aviso: lock apartado en %s (el montaje anterior sigue en mods_respaldo)" % destino)


class TreePublish:
    """Un escritor, staging fuera de mods, backups únicos; nunca borra datos.

    Lock pendiente tras crash: no apropiárselo automáticamente. Consultar el
    diario y recuperar con el juego cerrado antes de otra escritura.
    """
    def __init__(self, target):
        self.target = Path(target).absolute()
        self.store = self.target.parent.parent / "mods_respaldo"
        if self.target.is_symlink() or self.store.is_symlink():
            raise ValueError("Destino o respaldo enlazado")
        self.store.mkdir(exist_ok=True)
        if self.store.stat().st_dev != self.target.parent.stat().st_dev:
            raise ValueError("Staging y destino deben estar en el mismo volumen")
        self.token = uuid.uuid4().hex
        self.lock = self.store / (self.target.name + ".lock")
        try:
            self.lock.mkdir()  # falla si otro escritor o crash pendiente
        except FileExistsError:
            # H7 24/10: un corte a mitad dejaba el lock para siempre (nadie llamaba a recover_tree) y ningún
            # "Jugar" posterior reconstruía. Si su dueño ya no vive, se recupera por el diario y se reintenta.
            # DS193 28/10: quedaban dos caminos que seguían bloqueando "Preparando los personajes" (issue #53):
            #   a) el corte ocurrió entre lock.mkdir() y la escritura de owner.json -> no había dueño que leer
            #      (FileNotFoundError/JSON inválido) y roster_build moría en cada intento;
            #   b) el diario no se podía verificar -> recover_tree lanzaba y el lock quedaba para siempre.
            # En ambos casos el destino (mods/_roster) NO se ha tocado todavía (el commit es el que mueve el
            # árbol), así que se aparta el lock a mods_respaldo (nunca se borra) y se sigue con uno nuevo.
            owner = _leer_owner(self.lock)
            if owner and _pid_vivo(owner.get("pid", 0)):
                raise
            recuperado = False
            if owner and owner.get("run"):
                try:
                    recover_tree(self.target, owner["run"], readers_stopped=True)
                    recuperado = True
                except Exception as exc:  # diario truncado, lock de otro lote, respaldo cambiado...
                    print("aviso: no se pudo recuperar el montaje anterior (%s); el lock se aparta" % exc)
            else:
                print("aviso: lock sin dueño legible (%s); se aparta" % self.lock)
            if not recuperado:
                _apartar_lock(self.lock, self.store, self.target.name)
            self.lock.mkdir()
        self.run = self.store / (self.target.name + "-" + self.token)
        self.run.mkdir()
        with (self.lock / "owner.json").open("x", encoding="utf-8") as stream:
            json.dump(dict(token=self.token, run=str(self.run), pid=os.getpid()), stream)
        self.stage = self.run / "staging"
        self.stage.mkdir()
        self.backup = self.run / "anterior"
        self.state = "preparado"
        self.closed = False
        self.record()

    def record(self, **extra):
        # Registros append-only: un crash no trunca el estado anterior.
        value = dict(state=self.state, target=str(self.target), token=self.token,
                     pid=os.getpid(), **extra)
        with (self.run / "journal.jsonl").open("a", encoding="utf-8") as stream:
            stream.write(json.dumps(value, ensure_ascii=False) + "\n")
            stream.flush()
            os.fsync(stream.fileno())

    def _confirmar(self, estado, reparto, **extra):
        self.state = estado
        self.reparto_confirmado = reparto is not None
        try:
            self.record(**extra)
        except OSError as exc:
            if not self.reparto_confirmado:
                raise
            self._avisar_confirmado(exc)

    def _avisar_confirmado(self, exc):
        try:
            print("aviso: plantel y reparto confirmados; diario/lock pendiente (%s): %s" % (self.run, exc))
        except OSError:
            pass

    def commit(self, validate, reparto=None):
        # reparto: última escritura esencial, con os.replace como último paso.
        if self.closed or self.state != "preparado":
            raise ValueError("Publicación ya terminada o pendiente de recuperación")
        validate(self.stage)  # antes de mover el original
        # Indice de huellas del publicado (DBZ3_ROSTER_SIN_CACHE=1 lo apaga): el arbol viejo no se relee.
        indice = os.environ.get("DBZ3_ROSTER_SIN_CACHE") != "1"
        huellas = self.store / (self.target.name + ".huellas.json")
        firmas = {}
        stage_hashes = fingerprint(self.stage, firmas=firmas)
        if not stage_hashes:
            raise ValueError("No publicar árbol vacío")
        old_hashes = (fingerprint(self.target, _leer_huellas(huellas, self.target) if indice else None)
                      if self.target.exists() else {})
        self.state = "validado"
        self.record(new=stage_hashes, old=old_hashes)
        moved = publicado = False
        try:
            if self.target.exists():
                os.rename(self.target, self.backup)
                moved = True
            self.state = "anterior_apartado"
            self.record()
            os.rename(self.stage, self.target)
            publicado = True
            if reparto is not None:
                reparto()
        except (Exception, SystemExit) as exc:
            if reparto is None and not isinstance(exc, OSError):
                raise
            if publicado:
                self.state = "recuperacion_pendiente"
                os.rename(self.target, self.stage)
            if moved and not self.target.exists():
                os.rename(self.backup, self.target)
                self.state = "restaurado"
                self.record()
            elif publicado and not moved and not self.target.exists():
                self.state = "restaurado"
                self.record()
            else:
                self.state = "recuperacion_pendiente"
                self.record()
            raise
        self._confirmar("confirmado", reparto)
        if indice:
            _guardar_huellas(huellas, self.target, stage_hashes, firmas)
        return str(self.backup) if moved else None

    def retire(self, reparto=None):
        """Desactivar el generado cuando ya no hay fuentes; conservarlo fuera de mods."""
        if self.closed or self.state != "preparado":
            raise ValueError("Retirada ya terminada o pendiente")
        if self.target.exists():
            old = fingerprint(self.target)
            self.state = "retirada_validada"
            self.record(old=old, action="retire")
            os.rename(self.target, self.backup)
            if reparto is not None:
                try:
                    reparto()
                except (Exception, SystemExit):
                    os.rename(self.backup, self.target)
                    self.state = "restaurado"
                    self.record()
                    raise
            self._confirmar("retirado", reparto, old=old, action="retire")

    def finish(self):
        if self.closed:
            return
        # Una recuperación incompleta mantiene el lock, no oculta el problema.
        if self.state in ("anterior_apartado", "recuperacion_pendiente", "validado", "retirada_validada"):
            return
        try:
            os.rename(self.lock, self.run / "lock_finalizado")
        except OSError as exc:
            if not getattr(self, "reparto_confirmado", False):
                raise
            self._avisar_confirmado(exc)
        else:
            self.closed = True


def validate_roster(stage):
    try:
        import tomllib
    except ImportError:
        import tomli as tomllib
    stage = Path(stage)
    with (stage / "roster.toml").open("rb") as stream:
        doc = tomllib.load(stream)
    if not isinstance(doc, dict) or not (stage / "manifest.txt").is_file():
        raise ValueError("Roster sin manifiesto")
    if not (stage / "build.hash").read_text().strip():
        raise ValueError("Roster sin huella")
    # El validador binario (AFS/LZX) del Kit sigue siendo una puerta adicional.


def retire_tree(target):
    job = TreePublish(target)
    try:
        job.retire()
    finally:
        job.finish()


def pack_children(extracted):
    """Clasificación conservadora; documentos quedan en la extracción original."""
    root = Path(extracted)
    def docs(p):
        name = p.name.casefold()
        return (name.startswith(("readme", "leeme", "license", "licencia")) or
                name in (".ds_store", "desktop.ini"))
    def directories(p):
        return sorted((c for c in p.iterdir() if c.is_dir() and c.name != "__MACOSX"),
                      key=lambda c: c.name)
    for _ in range(16):
        nested = root / "mods"
        if nested.is_dir():
            if any(c.is_file() and not docs(c) for c in nested.iterdir()):
                raise ValueError("Pack trae archivos globales en mods: adaptador específico necesario")
            return directories(nested)
        # Una fuente, roster o mod compilado es hoja, aunque solo tenga un directorio.
        markers = ("personaje.toml", "traje.toml", "roster.toml", "manifest.txt", ".disabled")
        if any((root / m).exists() for m in markers):
            return [root]
        children = directories(root)
        if any(c.name.casefold() in ("us", "eu", "textures", "texture_packs") for c in children):
            return [root]
        loose = [c for c in root.iterdir() if c.is_file() and not docs(c)]
        if not loose and len(children) == 1:
            root = children[0]
            continue
        return [root]
    raise ValueError("Demasiadas envolventes: revisar paquete")


def install_pack(extracted, mods, validate, *, readers_stopped=False):
    """Publicación multi-mod con compensación; solo desde extracción ya validada.

    No es extractor ZIP. Conserva el paquete original, sus docs y los resultados
    fallidos. Todos los destinos se reservan antes de publicar el primero.
    """
    import shutil
    if not readers_stopped:
        raise ValueError("Parar lectores del guest antes de instalar")
    extracted, mods = Path(extracted), Path(mods)
    fingerprint(extracted)  # rechazar enlaces antes de copiar
    children = pack_children(extracted)
    if not children or len({p.name.casefold() for p in children}) != len(children):
        raise ValueError("Pack vacío o destinos duplicados")
    jobs = []
    committed = []
    try:
        for child in children:
            if child.name.startswith(".") or child.name.endswith(".disabled"):
                raise ValueError("Nombre de destino requiere normalización de activación")
            job = TreePublish(mods / child.name)
            jobs.append(job)
            # copytree solo escribe el staging NUEVO; jamás sobre el destino.
            shutil.copytree(child, job.stage, dirs_exist_ok=True)
            validate(job.stage)
        for job in jobs:
            job.commit(validate)
            committed.append(job)
        return [str(job.run) for job in jobs]
    except Exception:
        for job in reversed(committed):
            # Apartar también la versión publicada; nada se elimina al volver.
            os.rename(job.target, job.run / "publicado_revertido")
            if job.backup.exists():
                os.rename(job.backup, job.target)
            job.state = "restaurado"
            job.record()
        raise
    finally:
        for job in jobs:
            job.finish()


def _pid_vivo(pid):
    """True si el proceso existe (Windows: OpenProcess; os.kill(pid, 0) allí lo MATARÍA)."""
    if not pid:
        return False
    if os.name != "nt":
        try:
            os.kill(pid, 0)
        except ProcessLookupError:
            return False
        except PermissionError:
            return True
        return True
    import ctypes
    k = ctypes.windll.kernel32
    h = k.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
    if not h:
        return False
    code = ctypes.c_ulong()
    ok = k.GetExitCodeProcess(h, ctypes.byref(code))
    k.CloseHandle(h)
    return bool(ok) and code.value == 259  # STILL_ACTIVE


def recover_tree(target, run, *, readers_stopped=False):
    """Recuperación explícita al arrancar, nunca tomar un lock de un worker vivo.

    El llamador confirma que ya no existe el escritor/guest, y proporciona el
    destino esperado. Diario truncado final se conserva; se usa último registro
    completo. Si los hashes no permiten identificar los árboles, no mueve nada.
    """
    if not readers_stopped:
        raise ValueError("Recuperación exige escritores y lectores parados")
    target, run = Path(target).absolute(), Path(run).absolute()
    store = target.parent.parent / "mods_respaldo"
    if run.parent != store or run.is_symlink():
        raise ValueError("Diario no corresponde al destino")
    records = []
    for line in (run / "journal.jsonl").read_text(encoding="utf-8").splitlines():
        try:
            records.append(json.loads(line))
        except json.JSONDecodeError:
            break
    if not records or any(r.get("target") != str(target) for r in records):
        raise ValueError("Diario inválido o destino distinto")
    token = records[0]["token"]
    lock = store / (target.name + ".lock")
    if not lock.exists() and (run / "lock_finalizado").exists():
        return "ya_finalizado"
    owner = json.loads((lock / "owner.json").read_text(encoding="utf-8"))
    if owner.get("token") != token:
        raise ValueError("El lock pertenece a otro lote")
    validated = next((r for r in reversed(records) if "new" in r), None)
    backup = run / "anterior"
    retirement = next((r for r in reversed(records) if r.get("action") == "retire"), None)
    if retirement is not None:
        if target.exists() and fingerprint(target) == retirement["old"]:
            state = "retirada_no_publicada"
        elif not target.exists() and backup.exists() and fingerprint(backup) == retirement["old"]:
            state = "retirado_tras_crash"
        else:
            raise ValueError("Retirada no verificable: recuperación manual")
    elif validated is None:
        state = "abandonado_sin_publicar"
    elif target.exists():
        actual = fingerprint(target)
        if actual == validated["new"]:
            state = "confirmado_tras_crash"
        elif actual == validated["old"]:
            state = "anterior_intacto"
        else:
            raise ValueError("Destino cambiado: recuperación manual")
    elif backup.exists() and fingerprint(backup) == validated["old"]:
        os.rename(backup, target)
        state = "restaurado_tras_crash"
    elif not validated["old"]:
        state = "sin_destino_previo"
    else:
        raise ValueError("Respaldo no verificable")
    with (run / "recovery.jsonl").open("a", encoding="utf-8") as stream:
        stream.write(json.dumps(dict(state=state, token=token)) + "\n")
    os.rename(lock, run / "lock_finalizado")
    return state
