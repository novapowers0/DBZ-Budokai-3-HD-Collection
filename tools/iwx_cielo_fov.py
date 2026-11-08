"""eIW: prueba del FOV del cielo de Serpiente (IW 884) en el port. Solo prueba, copia smoke_e.

Congela la camara de la intro en el reloj del cuadro original n17215 (220.7995), lee los
campos de fov del gestor (+0x184 guion, +0x1C4 combate) y captura; despues escribe fov 0.6024
(el del #AMC del 884) en ambos y captura otra vez. Mide el perfil vertical del cielo.
Uso: DBZ3_AGENTE=eIW python tools/iwx_cielo_fov.py [--fov 0.6024]
"""
import argparse, json, os, struct, time, ctypes
from pathlib import Path
import pruebas as P
from iwx_bubbles_focal import HOME, pantalla2

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--indice', type=int, default=30)
    ap.add_argument('--fov', type=float, default=0.6024156808853149)
    ap.add_argument('--calentar', type=float, default=0.0)
    ap.add_argument('--copia', default='smoke_e')
    a = ap.parse_args()
    os.environ['DBZ3_AGENTE'] = 'eIW'
    os.environ.setdefault('DBZ3_TEST_FADE_WAIT', '5')
    out = HOME/f'cielo_fov_{time.time_ns()}'; out.mkdir()
    print('EVIDENCIA', out, flush=True)
    pantalla2()
    meta = {}

    def config(_):
        extra = {k: os.environ[k] for k in ('DBZ3_TEST_SALTAR_TEXTURA', 'DBZ3_NO_DIALOGS', 'DBZ3_IW_CIELO_ABS', 'DBZ3_IW_CIELO_TEST_X', 'DBZ3_NUBES_MULT', 'DBZ3_IW_CIELO_SIGUE', 'DBZ3_TEST_NUBES_DIR', 'DBZ3_IW_CIELO_VB') if k in os.environ}
        return dict(**extra, DBZ3_TEST_BATTLE=f'0,7,{a.indice}', DBZ3_COMBATE_MUNECO='quieto',
                    DBZ3_TEST_INTRO_SONDA='1', DBZ3_HUB_MODE='1', DBZ3_COMBATE_SONDA='1', DBZ3_TEST_IWX_FONDO_CAMARA=os.environ.get('EIW8_FONDO','0'),
                    DBZ3_TEST_RENDER='640x480', DBZ3_TEST_RENDER_ASPECT='4:3')

    def play(j, _):
        def read(addr, n):
            v = P.leer(j.h, P.GBASE+addr, n)
            if v is None or len(v) != n: raise P.Fallo('lectura incompleta')
            return v
        def write(addr, data):
            h = P.K32.OpenProcess(0x28, False, j.proc.pid)
            n = ctypes.c_size_t(); buf = ctypes.create_string_buffer(data)
            try:
                if not P.K32.WriteProcessMemory(ctypes.c_void_p(h), ctypes.c_void_p(P.GBASE+P.DESPL+addr), buf, len(data), ctypes.byref(n)):
                    raise ctypes.WinError(ctypes.get_last_error())
            finally:
                P.K32.CloseHandle(ctypes.c_void_p(h))
        def cam():
            mgr = int.from_bytes(read(0x823D6CF4, 4), 'big')
            if not mgr: return None
            b = read(mgr, 36000)
            u = lambda o: struct.unpack_from('>I', b, o)[0]
            f = lambda o: struct.unpack_from('>f', b, o)[0]
            return dict(mgr=mgr, estado=u(35128), modo=u(548), clip=u(560), acc=u(564), reloj=f(476), paso=f(464),
                        limite=f(576), fov184=f(0x184), fov1c4=f(0x1C4), ojo150=[f(0x150+4*i) for i in range(3)], pose=(list(struct.unpack('>8f', read(u(612), 32))) if mgr <= u(612) <= mgr+35900 else None))
        fin = time.monotonic()+90; c = None; grupo = -1; ult = None
        while time.monotonic() < fin:
            x = cam()
            if x and x['estado'] == 1 and x['modo'] == 7:
                if ult is None or x['acc'] != ult['acc'] or x['reloj'] < ult['reloj']:
                    grupo += 1
                ult = x
                if grupo == 1: c = x; break
            elif x and x['estado'] == 4: raise P.Fallo('intro terminada')
            time.sleep(.008)
        if not c: raise P.Fallo('no hay segundo recurso de camara')
        meta['intro'] = c
        mgr = c['mgr']
        def sostener(seg, fov=None):
            t0 = time.monotonic(); fin2 = t0+seg
            while time.monotonic() < fin2:
                paso1 = (time.monotonic()-t0) < a.calentar   # primero 0,6 s con paso 1 (el juego rehace la proyeccion), luego paso 0
                write(mgr+576, struct.pack('>f', 300.0))
                if not paso1: write(mgr+464, struct.pack('>f', 0.0))
                write(mgr+476, struct.pack('>f', 220.79949951171875))
                if fov is not None:
                    for off in (0x184, 0x1C4): write(mgr+off, struct.pack('>f', fov))
                time.sleep(.002)
        write(mgr+576, struct.pack('>f', max(c['limite'], 300.0)))
        write(mgr+464, struct.pack('>f', 0.0)); write(mgr+476, struct.pack('>f', 220.79949951171875))
        if os.environ.get('EIW_SERIE'):
            meta['serie'] = []
            for w in (0.3, 0.6, 1.0, 1.5, 2.5, 4.0):
                sostener(w)
                x = cam(); meta['serie'].append(dict(w=w, pose=x['pose'][:3], foto=str(j.captura(f'w{w}'))))
            return None
        sostener(float(os.environ['DBZ3_TEST_FADE_WAIT']))
        meta['antes'] = cam()
        po = meta['antes']['pose']; meta['pose_ok'] = bool(po) and abs(po[2]-2656.2649) < 0.01 and abs(po[0]-13.479) < 0.01
        meta['foto_base'] = str(j.captura('base')); sostener(.05)
        sostener(0.9, a.fov)
        meta['despues'] = cam(); meta['foto_fov'] = str(j.captura('fov'))
        sostener(.05, a.fov); meta['despues2'] = cam()
        for off, k in ((576, 'limite'), (476, 'reloj'), (464, 'paso')): write(mgr+off, struct.pack('>f', c[k]))
        return None

    def preparar(j, _par, _env):   # EIW_NIEBLA='COLOR,INI,FIN': cambia la niebla del 884 solo durante la prueba y la restaura
        import re
        ruta = j.dest/'mods'/'Campo IW 884'/'stage.toml'
        antes = ruta.read_bytes()
        ruta.write_text(re.sub(r'(?m)^niebla\s*=\s*"[^"]+"', 'niebla = "%s"' % os.environ['EIW_NIEBLA'], antes.decode('utf-8')), encoding='utf-8')
        cerrar = j.cerrar
        def restaurar():
            cerrar(); ruta.write_bytes(antes)
        j.cerrar = restaurar

    clave = 'eIW_cielo_fov'
    P.ESCENARIOS[clave] = ('Serpiente: fov del cielo (solo port)', config, play, preparar if os.environ.get('EIW_NIEBLA') else None)
    r = P.correr(a.copia, clave, [], None, {'modo': 'observar'}, True)
    (out/'resultado.json').write_text(json.dumps(dict(resultado=r, meta=meta), indent=1, default=str), encoding='utf-8')
    print(json.dumps(meta, default=str)[:1500]); print(P.tabla([r]))
    return 0 if r['ok'] else 1

if __name__ == '__main__':
    raise SystemExit(main())
