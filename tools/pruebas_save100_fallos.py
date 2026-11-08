"""Fallos de disco de Desbloquear todo; solo se carga desde pruebas.py."""
def registrar(g):
    # Comparte los ayudantes y las copias aisladas del arnes oficial.
    globals().update(g)
    ESCENARIOS["save100_fallos"] = (
        "solUNL2: errores de escritura/restauracion con SHA256 US/EU",
        configurar, fallos, _prep_save100)


def configurar(p):
    env = _cfg_save100(p)
    env["DBZ3_TEST_SAVE100_FAULTS"] = "1"
    return env


def fallos(j, p):
    root = j.dest / "user_data" / "dbz3"
    j.log(r"dbz3: launcher shown", 90)
    time.sleep(2)
    j.tecla_real("Escape")
    _save100_click(j, "open")
    originals = {s: h for s, h in _save100_hashes(root).items() if "respaldos_partida" not in s}
    native_paths = [f for f in root.glob("*/4E4D0856/**/*") if f.name in ("data.bin", "DBZ3.header")]
    for f in native_paths:
        dest = j.run / "originales" / f.relative_to(root)
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(f, dest)
    evidence = []

    def hashes():
        return {s: h for s, h in _save100_hashes(root).items()
                if "respaldos_partida" not in s and not s.endswith(".tmp")}

    def record(case, before, after, expected, text):
        evidence.append(dict(case=case, before=before, after=after, expected=expected,
                             ok=after == expected, message=text))
        (j.run / "hashes_fallos.json").write_text(json.dumps(evidence, indent=2), encoding="utf-8")
        if after != expected:
            raise Fallo(case + ": SHA256 distintos")
        j.info("PASS " + case + ": SHA256 correctos")

    def click(name, pattern):
        old = len(j.texto_log())
        _save100_click(j, name)
        deadline = time.time() + 20
        while time.time() < deadline:
            new = j.texto_log()[old:]
            if re.search(pattern, new):
                return new
            time.sleep(0.15)
        raise Fallo("sin resultado " + pattern)

    def restore(pattern):
        _save100_click(j, "restore")
        return click("confirm_restore", pattern)

    def reset(label):
        backups = root / "respaldos_partida"
        if backups.exists():
            shutil.move(str(backups), str(j.run / ("respaldos_" + label)))
        for src in (j.run / "originales").rglob("*"):
            if src.is_file():
                shutil.copy2(src, root / src.relative_to(j.run / "originales"))

    # Corruption and missing files must be refused before the first replacement.
    for damage in ("danado", "ausente", "inventario"):
        click("apply", r"save_100 applied:")
        backup = sorted((root / "respaldos_partida").glob("v2_*"))[-1]
        bad = next(backup.glob("*/4E4D0856/Headers/00000001/DBZ3.header"))
        if damage == "inventario":
            bad = backup / "targets"
        good = j.run / ("sano_" + damage)
        shutil.copy2(bad, good)
        if damage == "ausente":
            shutil.move(str(bad), str(j.run / "header_apartado"))
        else:
            # Same size, different bytes: size checks alone must not pass.
            b = bytearray(bad.read_bytes())
            b[len(b) // 2] ^= 0x5a
            bad.write_bytes(b)
        before = hashes()
        text = restore(r"save_100 (failed|restored):")
        record("respaldo_" + damage, before, hashes(), before, text)
        if "save_100 restored:" in text:
            raise Fallo("respaldo " + damage + " aceptado como sano")
        if "cannot be verified" not in text:
            raise Fallo("sin mensaje claro de respaldo danado")
        shutil.copy2(good, bad)
        text = restore(r"save_100 restored:")
        record("recuperar_" + damage, before, hashes(), originals, text)
        reset(damage)

    # Simulated ENOSPC after data.bin is committed and halfway through header.tmp.
    request = j.dest / "dbz3_save100_fault.req"
    request.write_text("quota", encoding="ascii")
    text = click("apply", r"save_100 failed:")
    record("cuota_aplicar", originals, hashes(), originals, text)
    if "Not enough disk space" not in text or "save_100 rollback: ok" not in text:
        raise Fallo("cuota sin rollback o mensaje claro")
    reset("cuota")

    # Real Windows sharing lock: reads for the backup work, replacement does not.
    create = K32.CreateFileW
    create.argtypes = [wt.LPCWSTR, wt.DWORD, wt.DWORD, ctypes.c_void_p, wt.DWORD, wt.DWORD, ctypes.c_void_p]
    create.restype = ctypes.c_void_p
    header = next(f for f in native_paths if f.name == "DBZ3.header")
    handle = create(str(header), 0x80000000, 1, None, 3, 0, None)
    if handle == ctypes.c_void_p(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        text = click("apply", r"save_100 failed:")
        record("bloqueado_aplicar", originals, hashes(), originals, text)
        if "close programs locking" not in text:
            raise Fallo("bloqueo sin mensaje claro")
    finally:
        K32.CloseHandle(handle)
    reset("bloqueo")

    # Exercise the separate rollback path of Restore, not just Apply.
    for failure in ("quota", "locked"):
        click("apply", r"save_100 applied:")
        before = hashes()
        handle = None
        if failure == "quota":
            request.write_text("quota", encoding="ascii")
        else:
            handle = create(str(header), 0x80000000, 1, None, 3, 0, None)
            if handle == ctypes.c_void_p(-1).value:
                raise ctypes.WinError(ctypes.get_last_error())
        try:
            text = restore(r"save_100 failed:")
            record("restaurar_" + failure, before, hashes(), before, text)
        finally:
            if handle:
                K32.CloseHandle(handle)
        text = restore(r"save_100 restored:")
        record("reintentar_" + failure, before, hashes(), originals, text)
        reset("restaurar_" + failure)

    # A cut is a real process exit, no caught exception / graceful rollback.
    request.write_text("cut", encoding="ascii")
    _save100_click(j, "apply")
    j.proc.wait(20)
    if j.proc.returncode != 73:
        raise Fallo("no se produjo el corte a mitad de escritura")
    (j.run / "corte.log").write_text(j.texto_log(), encoding="utf-8")
    interrupted = hashes()
    backup = sorted((root / "respaldos_partida").glob("v2_*"))[-1]
    for f in native_paths:
        if hashlib.sha256((backup / f.relative_to(root)).read_bytes()).hexdigest() != originals[str(f.relative_to(root))]:
            raise Fallo("corte sin originales integramente respaldados")
    j.cerrar()
    j.lanzar({"DBZ3_TEST_SAVE100_UI": "1", "DBZ3_TEST_SAVE100_FAULTS": "1"})
    j.log(r"launcher shown", 90)
    time.sleep(2)
    j.tecla_real("Escape")
    _save100_click(j, "open")
    text = click("apply", r"save_100 interrupted:")
    record("corte_rechaza_reaplicar", interrupted, hashes(), interrupted, text)
    text = restore(r"save_100 restored:")
    record("corte_recuperado", interrupted, hashes(), originals, text)
    j.info("PASS: todos los fallos forzados y recuperaciones")
