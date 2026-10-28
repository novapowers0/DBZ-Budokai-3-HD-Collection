"""Comprobacion sin juego del lock de publicacion (hornada_support.py).

Cubre el caso del issue #53: un corte a mitad de "Preparando los personajes" dejaba
mods_respaldo/_roster.lock y ningun "Jugar" posterior reconstruia. H7 recuperaba el
caso con diario valido; aqui se prueban los dos que quedaban:

  1) lock sin owner.json (el corte fue entre lock.mkdir() y la escritura del dueño);
  2) lock con owner.json ilegible (escritura a medias).

En los dos, TreePublish debe apartar el lock (a mods_respaldo, sin borrar nada) y
poder publicar. Uso:  python "mod center hd\\test_hornada_lock.py"
"""
import pathlib
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import hornada_support as H  # noqa: E402


def _caso(contenido_owner):
    root = pathlib.Path(tempfile.mkdtemp(prefix="dbz3_lock_"))
    (root / "mods").mkdir()
    lock = root / "mods_respaldo" / "_roster.lock"
    lock.mkdir(parents=True)
    if contenido_owner is not None:
        (lock / "owner.json").write_text(contenido_owner, encoding="utf-8")
    job = H.TreePublish(root / "mods" / "_roster")
    try:
        assert job.lock.is_dir(), "no se creo un lock nuevo"
        huerfanos = list((root / "mods_respaldo").glob("_roster.lock.huerfano-*"))
        assert len(huerfanos) == 1, "el lock viejo no se aparto: %r" % huerfanos
    finally:
        job.finish()
    assert (job.run / "lock_finalizado").is_dir(), "el lock nuevo no se cerro"


_caso(None)                 # corte antes de escribir owner.json
_caso("{dueño: roto")       # owner.json a medias
print("test_hornada_lock: OK")
