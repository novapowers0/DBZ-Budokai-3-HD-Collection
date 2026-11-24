# -*- coding: utf-8 -*-
"""Prueba de «Fusiones» (KIT-07b): texto del ajuste dbz3_fusion_duracion (mismo criterio que src/fusion.cpp
DuracionDe), escritura en un dbz3_user.toml de pruebas con copia y «Deshacer», y la linea de «Montar y probar».
    python kit/prueba_fusiones.py [carpeta_trabajo]   (por defecto E:\\DBZ3HD_trabajo\\kit_07b\\prueba)"""
import os
import shutil
import sys
import time
import types

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [os.path.dirname(HERE)]
from kit import fusiones as FU  # noqa: E402
from kit import nucleo, planes, probar  # noqa: E402

TRABAJO = sys.argv[1] if len(sys.argv) > 1 else r"E:\DBZ3HD_trabajo\kit_07b\prueba"


def main():
    # 1 texto del ajuste
    assert FU.componer() == "original"
    assert FU.componer("sin") == "todas=sin"
    assert FU.componer("corta") == "todas=50%"
    assert FU.componer("original", {"gogeta": "larga"}) == "68=200%,69=200%"
    assert FU.componer("sin", {"gogeta": "original"}) == "todas=sin,68=original,69=original"
    assert FU.componer("sin", {"gogeta": "sin"}) == "todas=sin"          # igual que todas: no se repite
    assert FU.componer("original", segundos={"gotenks": 45}) == "64=45s,65=45s"
    assert FU.componer("larga", segundos_general=90) == "todas=90s"
    # 2 lo que hara el juego (como DuracionDe: la ficha manda sobre «todas»; lo raro = original)
    v = "todas=sin,68=45s,72=50%"
    assert FU.regla(v, 68) == ("seg", 45) and FU.regla(v, 72) == ("pct", 50) and FU.regla(v, 64) == ("sin", None)
    assert FU.regla("original", 68) == ("original", None) and FU.regla("", 70) == ("original", None)
    assert FU.regla("todas=sin,68=original", 68) == ("original", None)
    assert FU.regla("sin", 76) == ("sin", None)                          # sin clave = todas
    assert FU.regla("todas=99999s", 1) == ("seg", 3600)
    # 3 ida y vuelta para la pantalla
    for g, por, sg, seg in (("sin", {}, 0, {}), ("corta", {"vegito": "larga"}, 0, {}),
                            ("original", {}, 0, {"kibito": 30}), ("original", {}, 120, {})):
        t = FU.componer(g, por, sg, seg)
        g2, sg2, por2, seg2 = FU.desde_texto(t)
        assert FU.componer(g2, por2, sg2, seg2) == t, (t, g2, sg2, por2, seg2)
    print("OK texto del ajuste")
    # 4 toml: solo cambia la linea del ajuste
    base = 'frame_cap = 60\ndbz3_game_dir = \'C:\\x\'\n'
    t1 = FU.con_ajuste(base, "todas=sin")
    assert t1 == base + 'dbz3_fusion_duracion = "todas=sin"\n', t1
    assert FU.con_ajuste(t1, "original") == base + 'dbz3_fusion_duracion = "original"\n'
    # 5 guardar y deshacer de verdad (carpeta de pruebas, nunca la del juego)
    if os.path.exists(TRABAJO):
        viejo = os.path.join(os.path.dirname(TRABAJO), "_viejos", time.strftime("%Y%m%d-%H%M%S"))
        os.makedirs(os.path.dirname(viejo), exist_ok=True)
        shutil.move(TRABAJO, viejo)
    juego = os.path.join(TRABAJO, "juego")
    os.makedirs(os.path.join(juego, "mods"))
    with open(os.path.join(juego, "dbz3_user.toml"), "w", encoding="utf-8") as fh:
        fh.write(base)
    env = types.SimpleNamespace(game=juego, mods=os.path.join(juego, "mods"),
                                backups=os.path.join(juego, "modkit_backups"))
    assert FU.ruta_toml(juego) == os.path.join(juego, "dbz3_user.toml")
    assert FU.leer_ajuste(juego) == "original" and FU.resumen(juego) == []
    plan = planes.Plan("fusiones", [("Duración", lambda: FU.guardar(env, FU.componer("sin"), nucleo.anotar_indice))])
    g = types.SimpleNamespace(anotar_indice=nucleo.anotar_indice, LANG="es")
    ok, texto, _lote = planes.ejecutar(g, env, plan, log=lambda t: None)
    assert ok, texto
    assert FU.leer_ajuste(juego) == "todas=sin"
    assert FU.resumen(juego) == ["✓ fusiones: duración sin límite"], FU.resumen(juego)
    assert FU.resumen(juego, True) == ["✓ fusions: duration no limit"]
    assert probar.resumen("", env.mods) == ["✓ fusiones: duración sin límite"], probar.resumen("", env.mods)
    print("OK guardar: %s" % FU.resumen(juego)[0])
    FU.guardar(env, FU.componer("original", {"gogeta": "larga"}, segundos={"gotenks": 45}), nucleo.anotar_indice)
    assert FU.resumen(juego) == ["✓ fusiones: Gogeta 200 % del original; Gotenks 45 s"], FU.resumen(juego)
    print("OK por fusión: %s" % FU.resumen(juego)[0])
    restaurados = nucleo.deshacer_ultimo(env)
    assert restaurados and FU.leer_ajuste(juego) == "todas=sin", restaurados
    nucleo.deshacer_ultimo(env)
    with open(os.path.join(juego, "dbz3_user.toml"), encoding="utf-8") as fh:
        assert fh.read() == base
    print("OK deshacer: el toml vuelve a estar como antes")
    # 6 Gogeta suelto (KIT-17): mod creado = plantilla probada en el juego, con copia y Deshacer
    ejemplo = r"E:\DBZ3HD_trabajo\kit_14\mods_b\gogeta_suelto\personaje.toml"
    plantilla = open(ejemplo, "rb").read() if os.path.isfile(ejemplo) else FU.PLANTILLA_GOGETA
    assert FU.PLANTILLA_GOGETA == plantilla, "la plantilla del Kit ya no es la probada"
    assert [x[0] for x in FU.SUELTOS if x[4]] == ["gogeta"]               # solo Gogeta se puede activar
    libres = FU.plazas_libres(env.mods)
    assert libres and libres > 0, libres                                   # roster_build estado real, mods vacio
    assert not FU.activo(env.mods)
    plan = planes.Plan("fusiones", [("Crear", lambda: FU.crear_gogeta(env, libres))], [FU.MOD_GOGETA])
    ok, texto, _l = planes.ejecutar(g, env, plan, log=lambda t: None)
    assert ok and FU.activo(env.mods), texto
    f = os.path.join(env.mods, FU.MOD_GOGETA, "personaje.toml")
    assert open(f, "rb").read() == plantilla and os.listdir(os.path.dirname(f)) == ["personaje.toml"]
    assert FU.datos_mod(os.path.dirname(f)) == (68, 3, "Gogeta")
    print("OK crear: mod idéntico byte a byte a la plantilla probada")
    nucleo.deshacer_ultimo(env)
    assert not FU.activo(env.mods), "deshacer no apartó el mod nuevo"
    print("OK deshacer la creación")
    plan = planes.Plan("fusiones", [("Crear", lambda: FU.crear_gogeta(env, libres))], [FU.MOD_GOGETA])
    planes.ejecutar(g, env, plan, log=lambda t: None)
    time.sleep(1.2)                                                            # un guardado = un lote (sello por segundo)
    ok, texto, _l = planes.ejecutar(g, env, planes.Plan("fusiones", [("Quitar", lambda: FU.quitar_gogeta(env, nucleo.anotar_indice))]),
                                    log=lambda t: None)
    assert ok and not FU.activo(env.mods), texto
    assert any(n.endswith("fusion_gogeta_suelto") for _d, _s, fs in os.walk(env.backups) for n in _s + fs), "no hay copia"
    nucleo.deshacer_ultimo(env)
    assert FU.activo(env.mods) and open(f, "rb").read() == plantilla
    print("OK apagar (mueve a la copia) y deshacer (lo devuelve)")
    shutil.rmtree(os.path.join(env.mods, FU.MOD_GOGETA))                      # solo la carpeta de pruebas
    # sin plazas: no se crea y se dice el motivo en llano
    try:
        FU.crear_gogeta(env, libres=0)
        raise SystemExit("debía negarse sin plazas")
    except RuntimeError as ex:
        assert "casilla libre" in str(ex) and "ficha" not in str(ex).lower(), ex
    assert not FU.activo(env.mods)
    viejo, FU.plazas_libres = FU.plazas_libres, lambda m: 0
    try:
        try:
            FU.crear_gogeta(env)
            raise SystemExit("debía negarse")
        except RuntimeError:
            pass
    finally:
        FU.plazas_libres = viejo
    assert "free slot" in FU.motivo_sin_plaza(True)
    print("OK sin plazas: no se crea el mod")
    # Montar y probar
    ok_m = "+ Gogeta: id 22, donante 0 (GOKOU), modelos [217, 219]"
    FU.crear_gogeta(env, libres)
    assert probar.resumen(ok_m, env.mods, [FU.MOD_GOGETA]) == ["✓ 1 personaje nuevo: Gogeta (fusionado)"]
    assert probar.resumen(ok_m, env.mods, [FU.MOD_GOGETA], True) == ["✓ 1 new character: Gogeta (fused)"]
    mal = probar.resumen("!! fusion_gogeta_suelto: sin plaza libre, omitido", env.mods, [FU.MOD_GOGETA])
    assert mal == ["✕ el personaje Gogeta (fusionado) se omitió: sin plaza libre, omitido"], mal
    assert probar.resumen("", env.mods, [FU.MOD_GOGETA])[0].startswith("✕"), "sin línea en el montaje"
    shutil.rmtree(os.path.join(env.mods, FU.MOD_GOGETA))
    print("OK Montar y probar")
    print("TODO OK")
    return 0


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(errors="backslashreplace")
    except (AttributeError, ValueError):
        pass
    sys.exit(main())
