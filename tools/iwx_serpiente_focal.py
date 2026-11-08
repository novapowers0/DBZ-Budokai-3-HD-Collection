"""Focal unica del candidato de esperas; restaura el override al cerrar el juego.

--offline verifica el bin archivado y el rechazo de otros SPX, sin instalar.
La rama viva usa el bloqueo/cierre de pruebas.py y solo la copia fija smoke_b.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import time

import iwx_escenarios as E
import solcam_pruebas as S
from iwx_bubbles_focal import FUENTE, pantalla2
from iwx_intro_analizar import analizar

P = S.P
HOME = Path('D:/tmp/solIWX')
ARCHIVO = HOME/'guion_intro_1793668911012471400/campo884.amb'


def sha(b):
    return hashlib.sha256(b).hexdigest()


def argumento_espera(codigo):
    """Ejecuta solo MOV/PUSH/POP del tramo; modos segun iw_spx.h:474-543.

    No es un interprete SPX completo. Rechaza cualquier otro opcode/modo y
    conserva centinelas debajo del argumento para detectar cambios de pila.
    """
    pc = 0
    r0 = 0x12345678
    pila = [0x13579bdf, 0x2468ace0, 100]

    def byte():
        nonlocal pc
        v = codigo[pc]
        pc += 1
        return v

    def valor():
        nonlocal pc
        modo = byte()
        if modo == 0:
            return r0
        if modo == 0x4b:
            return pila[-1]
        if modo in (0x10, 0x20):
            n = 1 if modo == 0x10 else 2
            if pc+n > len(codigo):
                raise ValueError('Inmediato incompleto')
            v = int.from_bytes(codigo[pc:pc+n], 'little', signed=True)
            pc += n
            return v
        raise ValueError(f'Modo de lectura no admitido: {modo:#x}')

    def escribir(v):
        nonlocal r0
        modo = byte()
        if modo == 0:
            r0 = v
        elif modo == 0x4b:
            pila[-1] = v
        else:
            raise ValueError(f'Destino no admitido: {modo:#x}')

    while pc < len(codigo):
        op = byte()
        if op == 0x01:
            escribir(valor())
        elif op == 0x08:
            pila.append(valor())
        elif op == 0x0a:
            v = pila[-1]
            escribir(v)
            pila.pop()
        else:
            raise ValueError(f'Opcode no admitido: {op:#x}')
    return r0, pila


def verificar(datos):
    nuevo = E.CI.intro_serpiente(datos)
    cambios = [i for i, (a, b) in enumerate(zip(datos, nuevo)) if a != b]
    o, s, _t, _f = E.EC.hijos_amb(datos, '>')[3]
    if len(nuevo) != len(datos) or cambios != [o+291, o+292, o+293, o+294, o+352, o+353]:
        raise ValueError('El candidato cambio algo distinto de los dos operandos')
    original = datos[o+291:o+301]
    corregido = nuevo[o+291:o+301]
    fallido = bytearray(original)
    fallido[2] = 200
    assert argumento_espera(original) == (100, [0x13579bdf, 0x2468ace0, 100, 100])
    assert argumento_espera(bytes(fallido)) == (-56, [0x13579bdf, 0x2468ace0, -56, -56])
    assert argumento_espera(corregido) == (200, [0x13579bdf, 0x2468ace0, 200, 200])
    toks = E.EC._spx_tokens(nuevo[o:o+s], struct.unpack_from('<I', nuevo, o+20)[0])
    assert (350, ('PUSH', 260)) in toks
    # Rechazar una variante, en lugar de aplicar offsets a otro guion.
    ajeno = bytearray(datos)
    ajeno[o+293] ^= 1
    try:
        E.CI.intro_serpiente(bytes(ajeno))
    except ValueError:
        pass
    else:
        raise AssertionError('No rechazo un SPX ajeno')
    return nuevo, dict(sha256_antes=sha(datos), sha256_candidato=sha(nuevo),
        offsets_cambiados=cambios, bytes=len(nuevo), solo_dos_esperas=True,
        inmediato8_fallido=-56, espera2_corregida=200, espera3_corregida=260,
        pila_preservada=True, limite_interprete='Solo el tramo MOV/PUSH/POP; no interpreta el guion completo',
        spx_ajeno_rechazado=True, habilitado_normal=False)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--offline', action='store_true')
    a = p.parse_args()
    os.environ.update(DBZ3_AGENTE='solIWX', PYTHONUTF8='1')
    out = HOME/f'serpiente_focal_{time.time_ns()}'
    out.mkdir()
    print('EVIDENCIA', out, flush=True)
    _, v = verificar(ARCHIVO.read_bytes())
    if a.offline:
        with (out/'offline.json').open('x', encoding='utf-8') as f:
            json.dump(v, f, indent=1)
        print(json.dumps(v, indent=1))
        return 0
    pantalla2()
    estado = dict(restaurado=False, instalado=False)

    def env(par):
        return {**S.entorno(par), 'DBZ3_TEST_INTRO_SONDA':'1', 'DBZ3_HUB_MODE':'1',
                'DBZ3_PS2_GAMES':FUENTE, 'DBZ3_TEST_TURBO':'1'}

    def preparar(j, _par, _env):
        ruta = j.dest/'mods/Campo IW 884/us/data_cmn.afs/5060/geom.bin'
        raw = ruta.read_bytes()
        datos = E.unpack(raw, out, 'instalado')
        candidato, verificacion = verificar(datos)
        respaldo = out/'geom_anterior.bin'
        respaldo.write_bytes(raw)
        amb = out/'candidato.amb'
        lzx = out/'candidato.lzx'
        amb.write_bytes(candidato)
        subprocess.run([str(E.EC.XBC), '/N:2048', str(amb), str(lzx)],
                       check=True, capture_output=True, timeout=120)
        raw_nuevo = lzx.read_bytes()
        # El guest lee bloques de 0x1000; conservar padding y permitir mid-insert.
        tamano = (max(len(raw), len(raw_nuevo)) + 0xfff) & ~0xfff
        raw_nuevo += bytes(tamano-len(raw_nuevo))
        if E.unpack(raw_nuevo, out, 'roundtrip') != candidato:
            raise ValueError('El roundtrip LZX no conserva el candidato')
        # No escribir sobre un override cambiado mientras se preparo el candidato.
        if ruta.read_bytes() != raw:
            raise ValueError('Cambio concurrente del override; cancelar')
        cerrar = j.cerrar

        def cerrar_y_restaurar():
            cerrar()
            if j.proc is not None and j.proc.poll() is None:
                raise RuntimeError('Juego propio aun vivo: no restaurar el recurso abierto')
            if estado['instalado'] and not estado['restaurado']:
                if sha(ruta.read_bytes()) != sha(raw_nuevo):
                    raise RuntimeError('Override cambiado por otro proceso; respaldo conservado')
                shutil.copy2(ruta, out/'geom_candidato_usado.bin')
                shutil.copy2(respaldo, ruta)
                estado['restaurado'] = ruta.read_bytes() == raw
                with (out/'restauracion.json').open('x', encoding='utf-8') as f:
                    json.dump(estado, f, indent=1)
        j.cerrar = cerrar_y_restaurar
        ruta.write_bytes(raw_nuevo)
        estado.update(instalado=True, ruta=str(ruta), sha256_instalado=sha(raw_nuevo),
                      sha256_respaldo=sha(raw))
        with (out/'candidato.json').open('x', encoding='utf-8') as f:
            json.dump({**verificacion, **estado}, f, indent=1)

    def jugar(j, par):
        S.ejecutar(j, par)

    clave = 'solIWX_serpiente_esperas'
    P.ESCENARIOS[clave] = ('Serpiente: dos esperas candidatas frente a IW EU', env, jugar, preparar)
    r = P.correr('smoke_b', clave, [], None,
                 {'modo':'observar', 'ids':'0,7,30', 'seg':'2'}, True)
    try:
        medida = analizar(r['run'])
        grupos = [g for g in medida['grupos'] if g['estado']==1 and g['modo']==7]
        duraciones = [g['pasos'] for g in grupos]
        with (out/'medida.json').open('x', encoding='utf-8') as f:
            json.dump(medida, f, indent=1)
        if duraciones != [199, 300, 360]:
            raise ValueError(f'Duraciones observadas {duraciones}, esperadas [199,300,360]; primer cuadro no muestreado')
        if not estado['restaurado']:
            raise ValueError('No consta restauracion del override')
        r['duraciones'] = duraciones
    except (OSError, ValueError, KeyError) as e:
        r['ok'] = False
        r['msg'] = (r['msg']+'; ' if r['msg'] else '')+str(e)
    with (out/'resultado.json').open('x', encoding='utf-8') as f:
        json.dump(r, f, indent=1, default=str)
    print(P.tabla([r]), flush=True)
    return 0 if r['ok'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
