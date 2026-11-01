"""Medida focal solPUL: espacio de Freezer y esquiva, sin forzar estados del juego."""
import argparse
import json
import os
from pathlib import Path
import struct
import time

os.environ['DBZ3_AGENTE'] = 'solPUL'
os.environ['PYTHONUTF8'] = '1'
import pruebas as P


def validar_alturas(filas):
    # El radio150 y la elevacion minima -20 grados dan alturas menores de100.
    # Excluir solo el retorno al suelo, no las esquivas hacia abajo validas.
    activas = [r for r in filas if r['pos'][1] > 20]
    assert len(activas) >= 20, 'sin suficientes muestras aereas'
    correctas = sum(abs(r['cintura'][1] - r['pos'][1]) < 2 for r in activas)
    assert correctas / len(activas) > .95, 'B3 sigue apoyando al rival en el mar'
    assert any(r['codigo'] == '0xfb9' for r in activas), 'esquiva393 no observada'
    print(f'ALTURAS PASS: {correctas}/{len(activas)} muestras a menos de2u; esquiva393 vista', flush=True)
    jugador = [r for r in activas if 'jugador_cintura' in r]
    if jugador:
        bien = sum(abs(r['jugador_cintura'][1] - r['jugador_pos'][1]) < 2 for r in jugador)
        assert bien / len(jugador) > .95, 'el suelo del rival desplaza a Krilin'
        print(f'KRILIN PASS: {bien}/{len(jugador)} muestras conservan su propia altura', flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('etiqueta')
    ap.add_argument('--copia', default='sol')
    ap.add_argument('--clave', default='0x7D7')
    ap.add_argument('--alturas', action='store_true')
    ap.add_argument('--cutins', action='store_true')
    ap.add_argument('--verificar-alturas', action='store_true')
    ap.add_argument('--validar-medida')
    a = ap.parse_args()
    if a.validar_medida:
        validar_alturas(json.loads(Path(a.validar_medida).read_text(encoding='utf-8'))['filas'])
        return
    out = Path('D:/tmp/solPUL') / a.etiqueta
    out.mkdir(parents=True, exist_ok=True)
    def env(_):
        e = {'DBZ3_TEST_IWMINI': a.clave + ',auto', 'DBZ3_PS2_GAMES': 'D:/DBZ3HD/ps2_games'}
        if a.alturas:
            e['DBZ3_TEST_PUL_ALTURAS'] = '1'
        if a.cutins:
            e['DBZ3_TEST_PUL_CUTINS'] = '1'
        return e
    def jugar(j, _):
        if a.cutins:
            from PIL import Image
            j.log(r'iwmini: entrada acabada', 150)
            for letra in ('C', 'D'):
                j.log(r'iwmini: aviso ' + letra + r' del indice 16', 90)
                p = j.captura('aviso_' + letra)
                with Image.open(p) as im:
                    im.resize((im.width // 2, im.height // 2)).save(out / ('aviso_' + letra + '_05.png'))
            (out / 'avisos.txt').write_text(j.texto_log(), encoding='utf-8')
            return
        j.log(r'iwdisparo: juego .* empieza', 150)
        filas = []
        foto = False
        fin = time.monotonic() + 22
        while time.monotonic() < fin:
            mgr, n = P._freecam_mem(j)
            if not mgr:
                P.dormir(.01)
                continue
            x = mgr + 2096 + 8240
            raw = P.leer(j.h, P.GBASE + x, 0xD00)
            if not raw:
                continue
            def f(o): return struct.unpack_from('>f', raw, o)[0]
            cod = struct.unpack_from('>I', raw, 0x650 + 0x520)[0] & 0xFFF
            jugador_pos = P.leer(j.h, P.GBASE + mgr + 2096 + 64, 12)
            jugador_cintura = P.leer(j.h, P.GBASE + mgr + 2096 + 1616, 12)
            fila = {'n': n, 'codigo': hex(cod), 'f': f(0x650 + 0x608),
                          'pos': [f(64), f(68), f(72)],
                          'cintura': [f(1616), f(1620), f(1624)],
                          'pecho': [f(1632), f(1636), f(1640)]}
            if jugador_pos and jugador_cintura:
                fila['jugador_pos'] = struct.unpack('>3f', jugador_pos)
                fila['jugador_cintura'] = struct.unpack('>3f', jugador_cintura)
            filas.append(fila)
            if not foto and time.monotonic() > fin - 19:
                from PIL import Image
                p = j.captura('espacio_freezer')
                with Image.open(p) as im:
                    im.resize((im.width // 2, im.height // 2)).save(out / 'port_05.png')
                foto = True
            P.dormir(.015)
        codigos = sorted({r['codigo'] for r in filas})
        alturas = [r['cintura'][1] for r in filas]
        resumen = {'muestras': len(filas), 'codigos': codigos,
                   'y_cintura_min': min(alturas, default=None), 'y_cintura_max': max(alturas, default=None),
                   'run': str(j.run)}
        (out / 'medida.json').write_text(json.dumps({'resumen': resumen, 'filas': filas}, indent=1), encoding='utf-8')
        print(json.dumps(resumen), flush=True)
        if a.verificar_alturas:
            validar_alturas(filas)
    nombre = 'solPUL_' + a.etiqueta
    P.ESCENARIOS[nombre] = ('Freezer: posicion/huesos/esquiva', env, jugar)
    r = P.correr(a.copia, nombre, [], None, {'timeout': '400'}, True)
    (out / 'resultado.json').write_text(json.dumps(r, indent=1, default=str), encoding='utf-8')
    print(P.tabla([r]), flush=True)


if __name__ == '__main__':
    main()
