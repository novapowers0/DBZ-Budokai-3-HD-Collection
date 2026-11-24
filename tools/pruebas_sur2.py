"""Escenarios SUR2 en pruebas.py. Ejecutar en una copia, con los diffs aplicados."""
import re
import sys
import tempfile
from pathlib import Path

if __name__ == '__main__':
    import pruebas as P
    vars(sys.modules[__name__]).update({k: v for k, v in vars(P).items() if not k.startswith('__')})


IDIOMAS = dict(en=1, ja=2, de=3, fr=4, es=5, it=6, pt=1)
# Mapa: selector de Survivors, actor, SKC, forma inicial HD, componentes locales SPX y comunes.
ULT = {
    'llanura': ('spirit_bomb', 33, 119, 2, (4, 5, 6, 7), ()),
    'desierto': ('death_ball', 27, 102, 4, (6, 15, 14, 7), (5,)),
    'glaciar': ('supernova', 38, 136, 1, (12, 15, 13), ()),
}
ESCENAS = ('sur2_kienzan', 'sur2_definitiva', 'sur2_definitiva_control', 'sur2_pantallas')


def registrar(p):
    def dato(par, nombre, defecto, opciones):
        v = par.get(nombre, defecto)
        if v not in opciones:
            raise p.Fallo(f'{nombre}={v!r}; esperado: {", ".join(opciones)}')
        return v

    def base(par, sonido=False):
        reg = dato(par, 'region', 'us', ('us', 'eu'))
        idioma = dato(par, 'idioma', 'es', IDIOMAS)
        ui = 'pt' if idioma == 'pt' else ''
        cfg = (f'dbz3_region="{reg}";dbz3_language={IDIOMAS[idioma]};dbz3_ui_language="{ui}";'
               f'audio_mute={str(not sonido).lower()};dbz3_mute={str(not sonido).lower()};dbz3_mute_unfocused=false')
        if sonido and par.get('audio'):
            audio = Path(par['audio'])
            ruta = audio.as_posix()
            if (not audio.is_absolute() or not audio.parent.is_dir() or audio.exists() or audio.suffix.lower() != '.f32'
                    or any(c in ruta for c in ';"\r\n')):
                raise p.Fallo('audio: ruta absoluta .f32 nueva en una carpeta existente')
            cfg += f';dbz3_audio_dump="{ruta}"'
        return cfg

    def surv(par, sonido=False):
        return dict(DBZ3_TEST_SURVIVORS_AUTO='1', DBZ3_TEST_SURVIVORS_SEMILLA='2024',
                    DBZ3_TEST_CFG=base(par, sonido) + ';dbz3_survivors_contenido_v2=true')

    def kien_env(par):
        mapa = dato(par, 'escenario', 'llanura', ('llanura', 'glaciar', 'desierto'))
        malla = dato(par, 'malla', '1', ('1', '0')) == '1'
        tex = dato(par, 'texturas', '1', ('1', '0')) == '1'
        tecnica = dato(par, 'tecnica', 'kienzan', ('kienzan', 'kienzan_eterno'))
        env = surv(par)
        env.update(DBZ3_TEST_SURVIVORS_PERSONAJE='4', DBZ3_TEST_SURVIVORS_DAR=tecnica,
                   DBZ3_TEST_SURVIVORS_ESCENARIO=mapa, DBZ3_TEST_SURVIVORS_INMORTAL='1',
                   DBZ3_TEST_SURVIVORS_DURA='40', DBZ3_TEST_SURVIVORS_TIEMPOS='1')
        env['DBZ3_TEST_CFG'] += (';dbz3_survivors_meta_v2=true'
                               f';dbz3_survivors_kienzan_malla={str(malla).lower()}'
                               f';dbz3_survivors_kienzan_texturas={str(tex).lower()}')
        return env

    def kien_preparar(j, par, env):
        # El arnés llama a prep antes de lanzar (y dos veces si carga una instantánea).
        if not hasattr(j, '_sur2_kienzan_guardado'):
            carpeta = Path(tempfile.mkdtemp(prefix='sur2_kienzan-', dir=j.run.resolve()))
            ruta = carpeta / 'progreso_prueba.toml'
            # En survivors_v2, Krillin es el índice 4: solo se activa su bit.
            with ruta.open('x', encoding='ascii') as f:
                f.write('luchadores = 16\n')
            j._sur2_kienzan_guardado = ruta
        env['DBZ3_TEST_SURVIVORS_GUARDADO'] = str(j._sur2_kienzan_guardado)

    def kien(j, par):
        especie = par.get('tecnica', 'kienzan')
        j.log(r'survivors: partida empezada personaje=(?:krilin)\b', seg=900)
        j.log(rf'survivors: prueba: tecnica dada {especie}\b', seg=15)
        patron = r'kienzan malla dibujada'
        reg = par.get('region', 'us')
        if par.get('malla', '1') == par.get('texturas', '1') == '1' and especie == 'kienzan':
            j.log(rf'{patron} region={reg} bsp=539 triangulos=98 dds=0,1\b', seg=15)
        else:
            p.dormir(4)
            if re.search(patron, j.texto_log()):
                raise p.Fallo('El control dibujó la malla de Kienzan')
        for n in range(3):
            j.captura(f'kienzan_{n + 1}')
            p.dormir(0.35)

    def ult_ficha(par):
        return ULT[dato(par, 'escenario', 'llanura', ULT)]

    def ult_ref_env(par):
        _, b3, skc, forma, _, _ = ult_ficha(par)
        # RequestBattleEx admite el índice de forma en el séptimo argumento. El escenario se valida con los ACE del guion.
        return dict(DBZ3_TEST_BATTLE=f'{b3},7,2,0,0,0,{forma},0', DBZ3_TEST_CAPS=str(skc),
                    DBZ3_TEST_ULT='1', DBZ3_TEST_KI_LLENO='1', DBZ3_TEST_SURVIVORS_ULT_TRAZA='1',
                    DBZ3_TEST_CFG=base(par, True) + ';dbz3_combate_prueba_muneco="quieto"')

    def ult_ref(j, par):
        nombre, _, _, _, local, comun = ult_ficha(par)
        reg = par.get('region', 'us')
        ramas = [f'banco=1 indice=(?:{"|".join(map(str, local))})']
        if comun:
            ramas.append(f'banco=7 indice=(?:{"|".join(map(str, comun))})')
        j.log(rf'sur2 ult: ace seq=\d+ region={reg} lado=0 ranura=\d+ (?:{"|".join(ramas)}) tipo=0400 ancla=grupo_escena', seg=150)
        j.captura(f'{nombre}_inicio')
        for n in range(4):
            p.dormir(2)
            j.captura(f'{nombre}_fase_{n + 1}')
        j.log(rf'sur2 ult: se_peticion seq=\d+ region={reg} banco=\d+ id=\d+', seg=12)
        if par.get('audio'):
            j.log(r'dbz3 audio: volcado abierto', seg=5)
            if Path(par['audio']).stat().st_size == 0:
                raise p.Fallo('Volcado de audio vacío')
        j.info('Referencia estática/visual: el log SE solo acredita peticiones; cotejar la grabación para asignar el sonido.')

    def ult_ctrl_env(par):
        ataque = ult_ficha(par)[0]
        env = surv(par, True)
        env.update(DBZ3_TEST_SURVIVORS_ATAQUE=ataque, DBZ3_TEST_SURVIVORS_SOLO_JEFE='1',
                   DBZ3_TEST_SURVIVORS_INMORTAL='1', DBZ3_TEST_SURVIVORS_DURA='120',
                   DBZ3_TEST_SURVIVORS_ESCENARIO=par.get('escenario', 'llanura'), DBZ3_TEST_SURVIVORS_JEFE_FASE2='5')
        env['DBZ3_TEST_CFG'] += ';dbz3_survivors_combate_oculto=true;dbz3_survivors_jefes_v2=true;dbz3_survivors_jefes_dos_fases=true'
        return env

    def ult_ctrl(j, par):
        ataque, _, skc, _, _, _ = ult_ficha(par)
        j.log(r'survivors: jefe fase=2\b', seg=900)
        j.log(rf'survivors: jefe ataque={ataque} fase=2 .* skc={skc}\b', seg=60)
        for n in range(3):
            j.captura(f'{ataque}_respaldo_{n + 1}')
            p.dormir(0.7)

    def pant_env(par):
        fin = dato(par, 'fin', 'derrota', ('derrota', 'victoria'))
        nativo = dato(par, 'nativo', '1', ('1', '0')) == '1'
        env = surv(par)
        env.update(DBZ3_TEST_SURVIVORS_CIERRE=fin, DBZ3_TEST_SURVIVORS_DURA='120',
                   DBZ3_TEST_SURVIVORS_ESCENARIO='llanura', DBZ3_TEST_SURVIVORS_SIN_SONIDO='1')
        env['DBZ3_TEST_CFG'] += f';dbz3_survivors_cierre_b3={str(nativo).lower()};dbz3_survivors_meta_v2=true'
        return env

    def pant(j, par):
        fin = par.get('fin', 'derrota')
        idioma = par.get('idioma', 'es')
        nativo = par.get('nativo', '1') == '1'
        j.log(r'survivors: partida empezada personaje=', seg=900)
        j.log(r'survivors: sur2 final de prueba', seg=20)
        j.log(rf'survivors: fin resultado={fin}\b', seg=5)
        if fin == 'derrota':
            if nativo:
                j.log(rf'survivors: sur2 derrota idioma={idioma} placa=1 beta=1', seg=3)
            else:
                j.log(r'survivors: anim jugador clip=5\b', seg=3)
            j.captura('derrota_pose')
        j.log(r'survivors: pantalla de resultados', seg=10)
        if nativo:
            j.log(rf'survivors: sur2 resultados resultado={fin} idioma={idioma} placa=1 marco=1 beta=1', seg=5)
        p.dormir(0.6)
        if not nativo and re.search(r'survivors: sur2 (derrota|resultados)\b', j.texto_log()):
            raise p.Fallo('El control entró en la interfaz SUR2')
        j.captura(f'resultados_{fin}_{idioma}')
        p._survivors_contar_hasta(j, 'Space', 'survivors: partida empezada personaje=')

    p.ESCENARIOS.update({
        'sur2_kienzan': ('SUR2: Kienzan normal original; comparación opciones ON / OFF en US/EU', kien_env, kien, kien_preparar),
        'sur2_definitiva': ('SUR2: captura de referencia ACE/SE del combate visible; no es aceptación audiovisual', ult_ref_env, ult_ref),
        'sur2_definitiva_control': ('SUR2: definitiva forzada de jefe en Survivors, registra el respaldo', ult_ctrl_env, ult_ctrl),
        'sur2_pantallas': ('SUR2: pose, resultados y BETA; idioma y derrota/victoria de prueba', pant_env, pant),
    })


if __name__ == '__main__':
    registrar(P)
    sys.exit(P.main())
