"""Escenarios SUR1 sobre el arnés pruebas.py (solo integrador; requieren juego)."""
import re
import sys

if __name__ == '__main__':
    import pruebas as P
    # Los módulos de Survivors usan el __main__ como interfaz de pruebas.py.
    vars(sys.modules[__name__]).update({k: v for k, v in vars(P).items() if not k.startswith('__')})
    import pruebas_survivors_p1, pruebas_survivors_p2  # noqa: F401

JEFES = {'llanura': ('kamehameha', 117, 518), 'desierto': ('death_beam', 100, 524),
         'glaciar': ('destructive_ray', 134, 521), 'ruinas': ('ill_flash', 124, 516)}


def registrar(p):
    def env(par):
        region = par.get('region', 'us')
        etapa = par.get('escenario', 'llanura')
        fx = par.get('fx', '1') == '1'
        if region not in ('us', 'eu') or etapa not in JEFES:
            raise p.Fallo('region o escenario invalido')
        return dict(DBZ3_TEST_SURVIVORS_AUTO='1', DBZ3_TEST_SURVIVORS_DURA='120',
                    DBZ3_TEST_SURVIVORS_ESCENARIO=etapa, DBZ3_TEST_SURVIVORS_SEMILLA='2024',
                    DBZ3_TEST_SURVIVORS_INMORTAL='1', DBZ3_TEST_SURVIVORS_JEFE_FASE2='60',
                    DBZ3_TEST_SURVIVORS_ATAQUE=JEFES[etapa][0], DBZ3_TEST_SURVIVORS_TIEMPOS='1',
                    DBZ3_TEST_CFG=f'dbz3_region="{region}";dbz3_survivors_combate_oculto=true;'
                                 f'dbz3_survivors_jefes_v2=true;dbz3_survivors_jefes_dos_fases=true;'
                                 f'dbz3_survivors_fx_jefe_nativo={str(fx).lower()};dbz3_survivors_motor_fx=true')

    def run(j, par):
        etapa = par.get('escenario', 'llanura')
        nombre, carta, bsp = JEFES[etapa]
        j.log(rf'survivors: jefe ataque={nombre} fase=1 .* skc={carta}\b', seg=1000)
        j.captura('sur1_fase1')
        j.log(r'survivors: jefe fase=2\b', seg=90)
        j.log(rf'survivors: jefe ataque={nombre} fase=2 .* skc={carta}\b', seg=60)
        j.captura('sur1_fase2')
        cuerpo = rf'survivors: FX cuerpo dibujado CST=0 bsp={bsp} .* lado=1'
        punta = rf'survivors: FX dibujado banco=2 indice=2 bsp={bsp} .* lado=1'
        if par.get('region', 'us') == 'us' and par.get('fx', '1') == '1' and etapa != 'ruinas':
            j.log(cuerpo, seg=25)
            j.log(punta, seg=25)
        else:
            p.dormir(4)
            if re.search(cuerpo + '|' + punta, j.texto_log()):
                raise p.Fallo('rayo nativo inesperado en el control')
        if re.search(r'survivors: jefe fase=3\b', j.texto_log()):
            raise p.Fallo('aparecio fase 3')
        j.captura('sur1_rayo')

    p.ESCENARIOS['sur1_jefe'] = ('SUR1: dos fases, ficha SKC, cuerpo y punta B3 (US), control EU/OFF', env, run)


if __name__ == '__main__':
    registrar(P)
    sys.exit(P.main())
