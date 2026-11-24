"""Adaptador del log existente mapa_log: repite el mando grabado, no inventa ticks."""
import math
import re

import mapa_log as ml
from original import CMD, ELF_HASH, NQ, me, paso, sha

GLOBO = re.compile(r'dbz3 niwa globo reloj=(\S+) fila=(-?\d+)')
DESBLOQUEO = re.compile(r'dbz3 iwmips mapa: fila (\d+) desbloqueada')


def cotejar_mapa(lines, afs=None):
    n = mapas = globos = moviendo = confirma = ndif = 0
    sp = globo = None
    dif, limites = [], set()

    def diferencia(li, campo, a, b, tolerancia=0):
        nonlocal ndif
        igual = abs(a - b) <= tolerancia if isinstance(a, float) else a == b
        if not igual:
            ndif += 1
            if len(dif) < 20:
                dif.append(dict(linea=li, campo=campo, original=a, port=b))

    if sha(me._elf()) != ELF_HASH:
        raise ValueError('ELF incompatible con repetición PAL')
    afs = afs or me.B.ELF.parent / 'USR' / 'DATA_CMN.AFS'
    for li, line in enumerate(lines, 1):
        if ini := ml.INI.search(line):
            _, road, saga, golpe, marcas, delta, cuadros, gx, gy = ini.groups()
            # Se reutiliza el constructor/formato nativo; se selecciona AFS PAL en vez del implícito USA de mapa_log.
            scd = me.entrada_afs(1127, afs)
            dmd = me.dmd_de(me.entrada_afs(1092 if road == '1' else 1093, afs))
            if dmd[:4] != b'#DMD' or scd[:4] != b'#SCD':
                raise ValueError('faltan DMD/SCD nativos para replay')
            sp = me.Espejo(saga=int(saga), road=road == '1', goku=(float(gx), float(gy)), dmd=dmd, scd=scd)
            for marca in marcas.strip(',').split(',') if marcas != '-' else ():
                k, mask = marca.split(':')
                sp.marca(int(k, 16), int(mask, 16))
            sp.hechas(())
            paso(sp.c, float(delta))
            diferencia(li, 'aparicion', sp.aparecer(de_vuelta=golpe == '1'), int(cuadros))
            sp.c.w32(sp.M + 0x338, 0xFFFFFFFF)
            globo = None
            mapas += 1
        elif sp is not None and (m := DESBLOQUEO.search(line)):
            fila = int(m[1])
            match = [sp.ctx + 0x30 * slot for slot, _, f, _, _ in sp.nodos() if f == fila]
            if len(match) != 1:
                limites.add('desbloqueo sin nodo único')
            else:
                a = match[0]
                # Sincroniza el hito externo. No convierte el momento del port en evidencia PAL del fundido.
                sp.c.w32(a + 4, sp.c.r32(a + 4) & ~4)
        elif m := GLOBO.search(line):
            if globo is not None:
                limites.add('globos de telemetría sin cuadro asociado')
            globo = float(m[1]), int(m[2])
        elif fr := ml.FR.search(line):
            if sp is None:
                limites.add('cuadro sin inicialización')
                continue
            _, mant, pulsa, lx, ly, delta, gx, gy, cx, cy, anim, sel, ordenes, conf = fr.groups()
            coords = list(map(float, (gx, gy, cx, cy, delta)))
            if not all(map(math.isfinite, coords)) or globo is not None and not math.isfinite(globo[0]):
                raise ValueError(f'telemetría no finita, línea {li}')
            c, m = sp.c, sp.M
            c.w32(NQ, 0)  # mismo límite de despacho que PasoMapa (el port conserva otra cola)
            paso(c, coords[-1])
            x, y, nodo = sp.cuadro(mant=int(mant, 16), pulsa=int(pulsa, 16), lx=int(lx), ly=int(ly))
            conf_ptr = c.r32(m + 0x1C)
            conf_fila = c.r16(conf_ptr) if any(c.r32(CMD + 4*j) == 9 for j in range(c.r32(NQ))) and conf_ptr else -1
            actual = (x, y, sp.f(m+0x29C), sp.f(m+0x2A0), c.r32(m+0x220), c.r16(nodo) if nodo else -1,
                      c.r32(NQ), conf_fila)
            for campo, a, b in zip(('x', 'y', 'cam_x', 'cam_y', 'anim', 'sel', 'ord', 'conf'), actual,
                                    (*coords[:4], *map(int, (anim, sel, ordenes, conf)))):
                diferencia(li, campo, a, b, 1e-3)
            if globo is not None:
                f = c.r32(m + 0x338)
                diferencia(li, 'globo_reloj', sp.f(m + 0x334), globo[0], 1e-3)
                diferencia(li, 'globo_fila', -1 if f == 0xFFFFFFFF else f, globo[1])
                globos += 1
                globo = None
            moviendo += int(int(lx) != 0 or int(ly) != 0)
            confirma += int(conf_fila >= 0)
            n += 1
    if globo is not None:
        limites.add('último globo sin cuadro')
    return dict(mapas=mapas, cuadros=n, globos=globos, mando_no_neutro=moviendo, confirma=confirma,
                completo=bool(n and globos == n and moviendo and confirma and not limites),
                limites=sorted(limites), diferencias=ndif, primeras=dif)
