"""Una focal UV884: cambia solo el reparto durante la prueba y lo restaura.

Observa primer vertice de111partes en RAM; no demuestra imagen GS equivalente.
"""
import argparse
from collections import Counter
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import sys
import threading
import time

import pruebas as P
from iwx_bubbles_focal import HOME, pantalla2


def sha(data):
    return hashlib.sha256(data).hexdigest()


def tree_hash(root):
    return {p.relative_to(root).as_posix(): sha(p.read_bytes())
            for p in root.rglob('*') if p.is_file()}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--indice', type=int, required=True)
    ap.add_argument('--montaje', type=Path, required=True)
    ap.add_argument('--imagenes-originales', type=Path,
        help='Reproduccion de dos relojes de camara; no sincroniza el reloj UV de pared')
    ap.add_argument('--reproducir-uv-original', action='store_true',
        help='Solo comparacion de dibujo: mult UV=0 e inyecta estados UV retenidos; NO valida el actualizador natural')
    a = ap.parse_args()
    if a.reproducir_uv_original and not a.imagenes_originales:
        ap.error('--reproducir-uv-original requiere --imagenes-originales')
    raw_audit = a.montaje.read_bytes()
    if sha(raw_audit) != '5ac92556991621e59854c64a212b519403c440cd42987d2643f6969e5d9b3788':
        raise ValueError('El montaje debe ser la auditoria autenticada de esta ronda')
    audit = json.loads(raw_audit)
    if not audit['auditoria_completa'] or audit['partes_candidato_distinto']:
        raise ValueError('Reparto previo no contrastado')
    os.environ['DBZ3_AGENTE'] = 'solIWX'
    out = HOME/f'fondo_uv_focal_{time.time_ns()}'
    out.mkdir()
    print('EVIDENCIA', out, flush=True)
    pantalla2()
    dest = P.BASE/'smoke_b'
    stage = dest/'mods'/'Campo IW 884'/'stage.toml'
    sys.path.insert(0, str(P.R/'mod center hd'))
    import escenarios_convertir as EC
    before_stage = stage.read_bytes()
    rows = {r['firma_sha256']: r for r in audit['partes']}
    original_lines = re.findall(r'^nubes\s*=\s*"([^"]+)"', before_stage.decode('utf-8'), re.M)
    if len(original_lines) != 111 or len(rows) != 111:
        raise ValueError('Se requieren111partes contrastadas')
    installed = (stage.parent/'us'/'data_cmn.afs'/'5060'/'geom.bin').read_bytes()
    native = EC.lzx(installed, False) if installed[:4] == b'\x0f\xf5\x12\xee' else installed
    zdd = next(native[o:o+s] for o, s, _t, _f in EC.hijos_amb(native, '>') if native[o:o+4] == b'#ZDD')
    parts = []
    changed = []
    for line in original_lines:
        fields = line.split(',')
        sig = bytes.fromhex(fields[0])
        r = rows[sha(sig)]
        offset = zdd.find(sig)
        if offset < 0 or zdd.find(sig, offset+1) >= 0 or int(fields[4]) != len(zdd) or int(fields[1]) != r['ventanas']:
            raise ValueError('Firma/conteo/tamano no autentica el ZDD instalado')
        for i, value in zip((2, 3, 5, 6), r['movimiento_candidato']):
            fields[i] = '%.7f' % value
        fields[7] = str(r['capa_candidata'])
        changed.append(','.join(fields))
        parts.append(dict(hijo=r['hijo'], offset_fuente=r['offset'], offset_zdd=offset,
            canal=r['canal_original'], valores=set()))
    iterator = iter(changed)
    candidate_stage = re.sub(r'(?m)^(nubes\s*=\s*")[^"]+(".*)$',
        lambda m: m[1]+next(iterator)+m[2], before_stage.decode('utf-8')).encode('utf-8')
    (out/'stage_anterior.toml').write_bytes(before_stage)
    (out/'stage_candidato.toml').write_bytes(candidate_stage)
    errors, samples, meta = [], Counter(), {}
    ready, stopped = threading.Event(), threading.Event()

    def config(_):
        env = dict(DBZ3_TEST_BATTLE=f'0,7,{a.indice}', DBZ3_COMBATE_MUNECO='quieto',
            DBZ3_TEST_INTRO_SONDA='1', DBZ3_COMBATE_SONDA='1',
            DBZ3_TEST_IWX_FONDO_CAMARA='0', DBZ3_HUB_MODE='1')
        if a.reproducir_uv_original:
            env['DBZ3_NUBES_MULT'] = '0'
        return env

    def prepare(j, par, env):
        if stage.read_bytes() != before_stage:
            raise P.Fallo('stage cambio antes de preparar: no sobrescribir')
        stage.write_bytes(candidate_stage)
        meta['stage_aplicado_sha256'] = sha(stage.read_bytes())

    def play(j, _):
        meta['exe_sha256'] = sha((j.dest/'dbz3.exe').read_bytes())

        def camera():
            def read(a, n):
                value = P.leer(j.h, P.GBASE+a, n)
                if value is None or len(value) != n:
                    raise P.Fallo('Lectura de camara incompleta')
                return value
            mgr = int.from_bytes(read(0x823D6CF4, 4), 'big')
            if not mgr:
                return None
            b = read(mgr, 36000)
            u = lambda o: struct.unpack_from('>I', b, o)[0]
            f = lambda o: struct.unpack_from('>f', b, o)[0]
            ptr = u(612)
            if not mgr <= ptr <= mgr+36000-64:
                return None
            pose = struct.unpack('>16f', read(ptr, 64))
            zraw = read(0x823A3DA0, 4)
            zdd_live = int.from_bytes(zraw, 'big')
            uv = None
            moving = next(p for p in parts if p['canal'] == 6)
            if 0x40000000 <= zdd_live < 0x50000000:
                uv = struct.unpack('>2f', read(zdd_live+moving['offset_zdd']+36, 8))
            return dict(mgr=mgr, estado=u(35128), clip=u(560), modo=u(548),
                acc=u(564),
                reloj=f(476), paso=f(464), limite=f(576), pose=list(pose), uv=uv,
                t_host=time.monotonic())

        def write(a, data):
            # Solo el proceso propio, bajo la copia bloqueada y el test explicito.
            h = P.K32.OpenProcess(0x28, False, j.proc.pid)
            if not h:
                raise ctypes.WinError(ctypes.get_last_error())
            try:
                n = ctypes.c_size_t()
                buf = ctypes.create_string_buffer(data)
                if not P.K32.WriteProcessMemory(ctypes.c_void_p(h),
                        ctypes.c_void_p(P.GBASE+P.DESPL+a), buf, len(data), ctypes.byref(n)) or n.value != len(data):
                    raise ctypes.WinError(ctypes.get_last_error())
            finally:
                P.K32.CloseHandle(ctypes.c_void_p(h))

        def photos():
            originals = json.loads(a.imagenes_originales.read_text(encoding='utf-8'))['originales']
            originals = [r for r in originals if r['n'] in (17215, 17216)]
            if len(originals) != 2:
                raise P.Fallo('Faltan las dos imagenes GS originales de la escena')
            end = time.monotonic()+90
            previous = None
            group = -1
            last = None
            meta['grupos_camara_observados'] = []
            while time.monotonic() < end:
                previous = camera()
                if previous and previous['estado'] == 1 and previous['modo'] == 7:
                    if last is None or previous['acc'] != last['acc'] or previous['reloj'] < last['reloj']:
                        group += 1
                        meta['grupos_camara_observados'].append(previous)
                    last = previous
                    if group == 1:
                        break
                elif previous and previous['estado'] == 4:
                    raise P.Fallo('Intro termina sin observar segundo recurso de camara')
                time.sleep(.008)
            else:
                raise P.Fallo('No aparece segundo recurso para reproducir relojes originales')
            mgr = previous['mgr']
            meta['reproduccion_camara'] = dict(antes=previous, pares=[],
                alteracion='Solo paso/reloj/limite de camara del proceso de prueba; UV sigue reloj de pared natural',
                reloj_uv_sincronizado=False, paridad_integral=False)
            if a.reproducir_uv_original:
                meta['reproduccion_camara'].update(
                    alteracion='Camara y estados UV originales reproducidos; actualizador UV natural multiplicado por cero',
                    estado_uv_reproducido=True, actualizador_uv_natural_validado=False)
            try:
                write(mgr+464, struct.pack('>f', 0.0))
                write(mgr+576, struct.pack('>f', max(previous['limite'], 300.0)))
                for orig in originals:
                    target = orig['tiempo_evaluado_inferido']
                    if target is None:
                        raise P.Fallo('Falta reloj evaluado anterior original')
                    write(mgr+476, struct.pack('>f', target))
                    expected_uv = None
                    if a.reproducir_uv_original:
                        current = camera()
                        moving = next(p for p in parts if p['canal'] == 6)
                        off = moving['offset_zdd']
                        count = rows[next(k for k, r in rows.items() if r['hijo'] == 1 and r['offset'] == 7712)]['ventanas']
                        raw = bytearray(P.leer(j.h, P.GBASE+meta['zdd']+off, count*44))
                        for i in range(count):
                            uv = struct.unpack_from('>2f', zdd, off+i*44+36)
                            value = tuple(x+y for x,y in zip(uv, orig['uv'][0]))
                            struct.pack_into('>2f', raw, i*44+36, *value)
                        expected_uv = list(struct.unpack_from('>2f', raw, 36))
                        write(meta['zdd']+off, bytes(raw))
                    espera = .65 if a.reproducir_uv_original else .12
                    # PRUEBA: DBZ3_TEST_FADE_WAIT=segundos alarga la espera antes
                    # de capturar (para que el fundido de entrada no contamine).
                    if espera_fija := os.environ.get('DBZ3_TEST_FADE_WAIT'):
                        try:
                            espera = max(espera, float(espera_fija))
                        except ValueError:
                            pass
                    time.sleep(espera)
                    before = camera()
                    picture = j.captura(f"original_{orig['n']}_camara_reproducida")
                    after = camera()
                    errors_pose = []
                    for value in (before, after):
                        if value['acc'] != previous['acc'] or value['estado'] != 1 or value['paso'] != 0 or value['reloj'] != target:
                            raise P.Fallo('Camara no permanece en el reloj original durante captura')
                        errors_pose.append(max(abs(x-y) for x,y in zip(value['pose'][:3]+value['pose'][4:7], orig['ojo']+orig['objetivo'])))
                    pair = dict(original_n=orig['n'], original_imagen=orig['imagen'],
                        tiempo_evaluado_inferido=target,
                        original_sha256=orig['imagen_sha256'], port_imagen=str(picture),
                        port_sha256=sha(Path(picture).read_bytes()), antes=before, despues=after,
                        error_ojo_objetivo_max=max(errors_pose),
                        camara_coincide=max(errors_pose) <= .001,
                        reloj_camara_coincide=True, reloj_uv_coincide=False,
                        limite='Camara reproducida para inspeccion; NO una pareja temporal UV ni aceptacion visual automatica')
                    if expected_uv is not None:
                        pair.update(uv_original_capa0=orig['uv'][0], uv_vertice_esperada=expected_uv,
                            uv_retenida_estable=list(before['uv']) == expected_uv == list(after['uv']),
                            sincronizacion='Estado UV y camara de referencia retenidos durante la captura; no reloj natural',
                            limite='Reproduccion controlada del estado de dibujo. No acredita tasa UV natural, FOV/aspecto ni fase EE/GS original')
                        if not pair['uv_retenida_estable']:
                            raise P.Fallo('UV reproducida no permanece estable durante captura')
                    meta['reproduccion_camara']['pares'].append(pair)
                    if not pair['camara_coincide']:
                        raise P.Fallo('Camara evaluada no coincide con la original')
            finally:
                for off, key in ((576, 'limite'), (476, 'reloj'), (464, 'paso')):
                    write(mgr+off, struct.pack('>f', previous[key]))

        def sample():
            validated = None
            try:
                while not stopped.wait(.04):
                    mgr_raw = P.leer(j.h, P.GBASE+0x823D6CF4, 4)
                    zraw = P.leer(j.h, P.GBASE+0x823A3DA0, 4)
                    if not mgr_raw or not zraw:
                        continue
                    mgr, live_zdd = int.from_bytes(mgr_raw, 'big'), int.from_bytes(zraw, 'big')
                    state_raw = P.leer(j.h, P.GBASE+mgr+35128, 4) if mgr else None
                    if not state_raw or not 0x40000000 <= live_zdd < 0x50000000:
                        continue
                    state = int.from_bytes(state_raw, 'big')
                    if state not in (1, 4):
                        continue
                    if validated != live_zdd:
                        for part in parts:
                            off = part['offset_zdd']
                            current = P.leer(j.h, P.GBASE+live_zdd+off, 36)
                            if not current or current[4:8]+current[12:36] != zdd[off+4:off+8]+zdd[off+12:off+36]:
                                raise ValueError('Ventana RAM no autentica offset/fuente')
                        validated = live_zdd
                        meta['zdd'] = live_zdd
                    for part in parts:
                        value = P.leer(j.h, P.GBASE+live_zdd+part['offset_zdd']+36, 8)
                        if not value or not all(abs(v) < 1e6 for v in struct.unpack('>2f', value)):
                            raise ValueError('UV ausente/no finita')
                        part['valores'].add(value.hex())
                    samples[state] += 1
                    if state == 1 and samples[state] >= 10:
                        ready.set()
            except Exception as exc:
                errors.append(f'{type(exc).__name__}: {exc}')
                ready.set()

        thread = threading.Thread(target=sample)
        thread.start()
        try:
            if not ready.wait(90):
                raise P.Fallo('No se observaron10muestras de intro')
            if a.imagenes_originales:
                photos()
            else:
                j.captura('intro_uv_candidato')
            P._esperar_pelea(j, 90)
            P.dormir(2)
        finally:
            stopped.set()
            thread.join()
        if errors:
            raise P.Fallo('; '.join(errors))

    P.ESCENARIOS['solIWX_uv_materiales'] = ('Serpiente: reparto UV111partes, candidato temporal', config, play, prepare)
    restored = False
    with P.copia_en_uso(dest):
        P.comprobar_libre(dest)
        user = dest/'user_data'
        previous_user = tree_hash(user)
        if user.exists():
            shutil.copytree(user, out/'user_data_anterior')
        toml = dest/'dbz3_user.toml'
        previous_toml = toml.read_bytes() if toml.exists() else None
        try:
            result = P._correr('smoke_b', 'solIWX_uv_materiales', [], None, {'timeout':'240'}, True)
        finally:
            if stage.read_bytes() == candidate_stage:
                stage.write_bytes(before_stage)
            elif stage.read_bytes() != before_stage:
                raise RuntimeError('stage cambio concurrentemente: respaldo preservado, revisar')
            if (out/'user_data_anterior').exists():
                shutil.move(str(user), str(out/'user_data_despues'))
                shutil.copytree(out/'user_data_anterior', user)
            if previous_toml is not None:
                toml.write_bytes(previous_toml)
            restored = stage.read_bytes() == before_stage and tree_hash(user) == previous_user
    changes = [dict(hijo=p['hijo'], offset_fuente=p['offset_fuente'], canal=p['canal'],
        valores_uv=len(p['valores'])) for p in parts]
    expected = all(p['valores_uv'] == 1 if p['canal'] is None else p['valores_uv'] > 1 for p in changes)
    ok = result['ok'] and restored and expected and not errors and samples[1] >= 20 and samples[4] >= 20
    report = dict(pasa=ok, resultado=result, meta=meta, muestras=dict(samples), errores=errors,
        partes=changes, quietas=sum(p['valores_uv'] == 1 for p in changes),
        moviles=sum(p['valores_uv'] > 1 for p in changes), stage_restaurado=restored,
        montaje_sha256=sha(raw_audit), geom_instalado_sha256=sha(installed),
        paridad_ps2=False, imagen_gs_comparada=False,
         limite='RAM asincrona del primer vertice de111partes; sin pareja PS2 ni prueba de dibujo completo/sonido')
    if a.imagenes_originales:
        report['limite'] = 'Dos capturas con camara original reproducida; inspeccion de dibujo, sin aceptar paridad integral/sonido'
    report['actualizador_natural'] = not a.reproducir_uv_original
    with (out/'resultado.json').open('x', encoding='utf-8') as f:
        json.dump(report, f, ensure_ascii=False, indent=1, default=str)
    print(json.dumps(dict(salida=str(out), pasa=ok, muestras=dict(samples),
        quietas=report['quietas'], moviles=report['moviles'], stage_restaurado=restored)))
    return 0 if ok else 1


if __name__ == '__main__':
    raise SystemExit(main())
