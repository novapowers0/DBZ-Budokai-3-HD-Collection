r"""prueba_rutas.py (T1B, mejora 6): ninguna herramienta ACTIVA de tools/ escribe la carpeta temporal vieja del disco D.

  python tools/prueba_rutas.py     -> sale 0 si bien, 1 si una herramienta activa nombra esa carpeta

Todo lo temporal va a E:\DBZ3HD_trabajo\tmp. VIEJAS = herramientas de un solo uso que ya la nombraban el 7/11: no se tocan
(si se reutiliza una, migrarla y quitarla de la lista). Una herramienta nueva o activa que la nombre hace fallar esta prueba.
"""
import re
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
PATRON = re.compile(r"D:[\/]+" + "tm" + "p", re.I)      # partido para que este archivo no se detecte a si mismo
VIEJAS = set("""
analiza_vblank.py
assets_censo.py
assets_censo_detalle.py
assets_censo_efectos_hd.py
astra_iw_audio_reloj.py
astra_iw_auditoria.py
astra_iw_ladrillos_port.py
astra_iw_msn019_port.py
astra_iw_original.py
astra_iw_port.py
astra_iw_rejilla_resumen.py
b2_huesos.py
b2s_medir.py
b2t_medir.py
b2u_medir.py
b2u_verificar.py
barrido_dif.py
campo_F_investigar.py
campo_F_pruebas.py
clip_gemini.py
comparar_padlog.py
compilar_solar_z.py
cpu_perfil/mkrsp.py
dif_lector_bubbles.py
dif_mapa_b2_pal.py
dif_mapa_iw_eu.py
dsPGPU_perf.py
dsPHIRES_medir.py
dsSERP_niebla.py
dsSERP_res.py
grabar_vitrina.py
inventario_modos.py
iw_animaciones_N.py
iw_animaciones_N_resumen.py
iw_ap_F_integrado_pruebas.py
iw_ap_F_integrar.py
iw_ap_F_investigar.py
iw_ap_F_pruebas.py
iw_audio_F_investigar.py
iw_audio_F_pruebas.py
iw_audio_F_reverb_original.py
iw_audio_F_reverb_pruebas.py
iw_campana.py
iw_capsulas_F4_datos.py
iw_capsulas_F4_original.py
iw_capsulas_F5_pcsx2.py
iw_capsulas_F5_rng.py
iw_capsulas_F_datos.py
iw_datos_Z.py
iw_entrega_Z.py
iw_estados_Z.py
iw_midi_F_datos.py
iw_mips/bubbles.py
iw_mips/cap_7d8.py
iw_mips/cap_7e1.py
iw_mips/fpu_hw.py
iw_mips/freezer.py
iw_mips/msn019.py
iw_mips/piccolo.py
iw_original_lote.py
iw_revision_Z.py
iw_sol9.py
iw_sol9c.py
iw_sol9c_original.py
iw_sol9c_pose.py
iwx_bubbles_evidencia.py
iwx_bubbles_focal.py
iwx_bubbles_inicio.py
iwx_censo.py
iwx_escenarios.py
iwx_intro_guion.py
iwx_original_cuadros.py
iwx_serpiente_focal.py
listo_noche.py
nGPU_mem.py
pcsx2_cuadros.py
pul2_original.py
pul_original.py
pul_pruebas.py
reponer_copias.py
satan_sol2_datos.py
satan_sol_medidas.py
solb2x_estados.py
solb2x_huesos_cmp2.py
solb2x_huesos_cmp3.py
solb2x_huesos_mapa.py
solb2x_original.py
solderx_amm.py
solderx_censo.py
solesc_original.py
solesc_resultados.py
test_solb2x_mirada.py
vanilla_online.py
""".split())


def culpables():
    out = []
    for f in sorted(TOOLS.rglob("*.py")):
        rel = f.relative_to(TOOLS).as_posix()
        if rel in VIEJAS:
            continue
        if PATRON.search(f.read_text(encoding="utf-8", errors="replace")):
            out.append(rel)
    return out


def main():
    mal = culpables()
    # la propia prueba: detecta una ruta mala y deja pasar una buena
    assert PATRON.search("D:" + "/tm" + "p/x") and not PATRON.search("E:/DBZ3HD_trabajo/tmp/x")
    for m in mal:
        print("RUTA VIEJA en herramienta activa:", m)
    print("AUTOPRUEBA rutas %s (%d viejas toleradas)" % ("FALLA" if mal else "OK", len(VIEJAS)))
    return 1 if mal else 0


if __name__ == "__main__":
    sys.exit(main())
