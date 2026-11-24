# SPDX-License-Identifier: MIT
"""F1: decisiones de orbita Viewer7D8 EU/USA, sin juego ni C++.

Dos dominios separados: digital con camara sobre ejes y analogico hacia +X
con atan2/sin/cos/acos originales; y fronteras con atan2/acos controlados.
La rotacion final solo se solicita: NO se modifica la camara ni se demuestra
su trayectoria. Los continuos son secuencias de solicitudes, no gameplay.
"""
import argparse
from collections import Counter
import itertools
import json
from pathlib import Path
import struct
import time

import disparo_arranque as arranque
import disparo_geometria as geometria
import disparo_viewer_agotamiento as ag
import disparo_viewer_pose as pose
import ee

base, arit, banco = pose.base, pose.arit, pose.banco
bits, leer, escribir = base.bits, base.leer, base.escribir
LO, HI = 0x3EB2B8C3, 0x4032B8C3
ANGULOS = (0, LO - 1, LO, LO + 1, bits(1), HI - 1, HI, HI + 1, bits(3.2))
TINY = (0, 0x30800000, 0xB0800000)
EJES = ((0, 1, 0), (0, 0, 1), (0, -1, 0))
SERVICIOS = ('hit', 'vista', 'camget', 'normalizar', 'dot', 'extender', 'acos',
             'truncar', 'atan2', 'sin', 'cos', 'solicitud', 'rotacion')
PERFILES = {
    'eu': dict(orbita=0x3245D0, hit=0x32C210, vista=0x2ED7E0, camget=0x2ED7D0,
               solicitud=0x2ED9D0, frente=0x480C10, velocidad=0x5D97D4,
               hitsp=0x49CDF4,
               mando=0x4ADECC, mando2=0x4ADF2C, analog=0x4ADEDA),
    'us': dict(orbita=0x3242E0, hit=0x32BEF0, vista=0x2ED5D0, camget=0x2ED5C0,
               solicitud=0x2ED7C0, frente=0x480460, velocidad=0x5D8FD4,
               hitsp=0x49C5D4,
               mando=0x4AD6CC, mando2=0x4AD72C, analog=0x4AD6DA)}


def doble(b):
    # Un float32 normal/cero cabe exactamente en double. No usa ee.fv/fb.
    return struct.unpack('<Q', struct.pack('<d', float(arit.racional(b))))[0]


def multiplicar(a, b):
    return arit.producto(a, b, cop1=True) if arit.racional(a) * arit.racional(b) else (a ^ b) & 0x80000000


def poner(ref, addr, dato):
    ag.poner_bytes(ref, addr, dato)


class OrbitaViewer(base.ResultadoViewer):
    def __init__(self, region, elf=None):
        super().__init__(region, elf)
        if ee.FPU_HW != set(ee.FPU_HW_SERIE) or ee.FPU_CERCANO or ee.VU_CERCANO or ee.FPU_SUMA_PS2:
            raise ValueError('se requiere perfil EE/VU de serie')
        self.p = p = dict(self.p, **PERFILES[region])
        c = self.cpu
        p['deadzone'] = c.gp - 0x7A6C
        p['semillap'], p['pasop'] = c.gp - 0x7FE8, c.gp - 0x7614
        js = (p['hit'], 0x142540, 0x119BB0, 0x119BB0, 0x1177C8, 0x11A6B0,
              0x117688, 0x117438, p['vista'], 0x142540, 0x1425A0, 0x119BB0,
              0x1177B0, 0x11A6B0, p['solicitud'], p['vista'], 0x142540,
              0x1425A0, 0x119BB0, 0x1177B0, 0x11A6B0, *([p['solicitud']] * 4))
        if base.llamadas(c.m, p['orbita'], p['orbita'] + 0x3F0) != js:
            raise ValueError('25 JAL de orbita fuera del perfil regional')
        if (c.r32(p['orbita'] + 0x20) != (0x78420000 | (p['frente'] & 0xFFFF))
                or c.r32(p['hit']) != (0x8F830000 | ((p['hitsp'] - c.gp) & 0xFFFF))
                or c.r32(p['solicitud'] + 4) != ((2 << 26) | (0x127160 >> 2))
                or [c.r32(p['frente'] + 4 * i) for i in range(4)] != [0, bits(1), 0, 0]):
            raise ValueError('vector frontal o salto de rotacion fuera del perfil')
        if [c.r32(p['velocidad'] + 4 * i) for i in range(2)] != [0x3FD33333, bits(1)]:
            raise ValueError('velocidades originales distintas tras init')
        rangos = [(p['orbita'], p['orbita'] + 0x3F0), (p['hit'], p['hit'] + 0x20),
                  (p['vista'], p['vista'] + 0x30), (p['camget'], p['camget'] + 0x10),
                  (p['solicitud'], p['solicitud'] + 0x10), (0x142540, 0x142570),
                  (0x1425A0, 0x1425C0), (0x1177C8, 0x1177E0), (0x114008, 0x1142C8)]
        rangos += list(geometria.MATH) + [(a, b) for _, a, b in arranque.TRIG]
        self.intervalos = list(dict.fromkeys(self.intervalos + rangos))
        c.whitelist.update(a for a, _ in self.intervalos)
        self.hashes = [base.huella(c.m[a:b]) for a, b in self.intervalos]
        self.roles.update({p[k]: k for k in ('hit', 'vista', 'camget', 'solicitud')})
        self.roles.update({0x142540: 'normalizar', 0x1425A0: 'dot', 0x119BB0: 'extender',
                           0x1177B0: 'acos', 0x11A6B0: 'truncar', 0x1177C8: 'atan2',
                           0x117688: 'sin', 0x117438: 'cos', 0x127160: 'rotacion'})
        for a, _ in self.intervalos:
            self.roles.setdefault(a, f'math_{a:08x}')
        c.hooks[0x127160] = self._rotacion
        self.controlado = False
        self.tilt, self.angulo_stick = 0, 0
        self.datos = {a: bytes(c.m[a:a + n]) for a, n in ((p['frente'], 16), (p['velocidad'], 8))}

    def _rotacion(self, c):
        if c.r[4] & ee.M32 != base.CAM + 0x80:
            raise RuntimeError('solicitud de rotacion para camara ajena')
        self.eventos.append(dict(tipo='rotacion_solicitada', destino=base.CAM + 0x80,
                                 fargs_bits=c.f[12:15]))
        c.r[2] = 0

    def _acos(self, c):
        self.eventos.append(dict(tipo='acos_controlado', argumento_double=c.r[4], retorno_double=doble(self.tilt)))
        c.r[2] = doble(self.tilt)

    def _atan2(self, c):
        self.eventos.append(dict(tipo='atan2_controlado', argumentos_double=[c.r[4], c.r[5]],
                                 retorno_double=doble(self.angulo_stick)))
        c.r[2] = doble(self.angulo_stick)

    def preparar_orbita(self, hit=0, deadzone=16, eje=(0, 0, 1), controlado=False,
                        tilt=0, angulo_stick=0, paso=bits(1)):
        if (type(hit) is not int or not 0 <= hit <= ee.M32
                or type(deadzone) is not int or not 0 <= deadzone <= 127
                or type(eje) is not tuple or eje not in EJES or type(controlado) is not bool
                or type(tilt) is not int or tilt not in ANGULOS
                or type(angulo_stick) is not int or angulo_stick not in TINY
                or type(paso) is not int or paso not in pose.PASOS
                or (not controlado and (tilt != 0 or angulo_stick != 0))):
            raise ValueError('fixture de orbita fuera del dominio revisado')
        ref = self.preparar(0, 0)
        c, p = self.cpu, self.p
        self.controlado, self.tilt, self.angulo_stick = controlado, tilt, angulo_stick
        for a, fn in ((0x1177B0, self._acos), (0x1177C8, self._atan2)):
            if controlado:
                c.hooks[a] = fn
            else:
                c.hooks.pop(a, None)
        for a, n in ((base.CTX + 0x120, 0x30), (p['mando'] - 16, 0x30),
                     (p['mando2'] - 16, 0x30), (p['deadzone'] - 16, 0x24),
                     (p['semillap'] - 16, 0x24)):
            c.m[a:a + n] = bytes([0xA5]) * n
            ref[a] = bytearray(c.m[a:a + n])
        for a, b in self.datos.items():
            ref[a] = bytearray(b)
        for a, v in ((base.CTX + 0x130, hit), (p['deadzone'], deadzone),
                     (p['semillap'], 0x12345678), (p['pasop'], paso)):
            c.w32(a, v)
            if a == p['pasop']:
                ref[a] = bytearray(struct.pack('<I', v))
            else:
                escribir(ref, a, v)
        c.w32(p['hitsp'], base.CTX)
        ref[p['hitsp']] = bytearray(struct.pack('<I', base.CTX))
        for a, xyz in ((base.CAM + 0x80, (0, 0, 0)), (base.CAM + 0x90, eje)):
            for i, v in enumerate((*xyz, 1)):
                c.w32(a + 4 * i, bits(v)); escribir(ref, a + 4 * i, bits(v))
        return ref

    def entradas(self, ref, mando=0, mando2=0, stick=(0, 0)):
        if (not self.preparado or type(stick) is not tuple or len(stick) != 2
                or any(type(v) is not int or not -128 <= v <= 127 for v in stick)
                or any(type(v) is not int or not 0 <= v <= 0xFFFF for v in (mando, mando2))):
            raise ValueError('mando/stick fuera del fixture')
        p, c = self.p, self.cpu
        dz = c.r32(p['deadzone'])
        analog = any(abs(v) > dz for v in stick)
        if not self.controlado and analog and not (stick[0] > dz and stick[1] == 0):
            raise ValueError('analogico nativo limitado a +X; resto requiere frontera controlada')
        for a, data in ((p['mando'], struct.pack('<H', mando)),
                        (p['mando2'], struct.pack('<H', mando2)),
                        (p['analog'], struct.pack('<bb', *stick))):
            c.m[a:a + len(data)] = data; poner(ref, a, data)

    def paso_orbita(self):
        c, p = self.cpu, self.p
        if (not self.preparado or c.r32(p['ctxp']) != base.CTX or c.r32(p['hitsp']) != base.CTX
                or c.r32(p['camp']) != base.CAM
                or c.r32(p['deadzone']) > 127):
            raise ValueError('contexto de orbita alterado')
        self.eventos = []
        c.llamadas.clear(); c.io.clear(); c.sin_puente.clear()
        c.call(p['orbita'], limite=30000)
        if c.io or c.sin_puente or any(a not in self.roles for a in c.llamadas):
            raise RuntimeError('E/S o llamadas ajenas a orbita')
        return dict(eventos=self.eventos.copy(), llamadas={k: sum(n for a, n in c.llamadas.items() if self.roles[a] == k)
                    for k in SERVICIOS if any(self.roles[a] == k for a in c.llamadas)})


def modelo(s, ref):
    # No lee salidas de la CPU. La camara sigue siendo entrada controlada y
    # la frontera final no la modifica: no atribuirle una trayectoria real.
    r, p = {a: bytearray(b) for a, b in ref.items()}, s.p
    def u16(a):
        for start, data in r.items():
            if start <= a and a + 2 <= start + len(data):
                return struct.unpack_from('<H', data, a - start)[0]
        raise ValueError('halfword fuera de referencia')
    def byte(a):
        for start, data in r.items():
            if start <= a < start + len(data):
                return struct.unpack_from('<b', data, a - start)[0]
        raise ValueError('byte fuera de referencia')
    h, dz = leer(r, leer(r, p['hitsp']) + 0x130), leer(r, p['deadzone'])
    vel = leer(r, p['velocidad'] + (4 if 3 <= h < 0x80000000 else 0))
    x, y = byte(p['analog']), byte(p['analog'] + 1)
    analog = abs(x) > dz or abs(y) > dz
    calls = Counter(hit=1, vista=1, normalizar=1, dot=1, extender=1, acos=1, truncar=1)
    events = []
    raw = arit.diferencia([leer(r, base.CAM + 0x90 + 4 * i) for i in range(3)],
                          [leer(r, base.CAM + 0x80 + 4 * i) for i in range(3)])
    v = arit.escalar(raw, bits(1))
    dots = [arit.producto(b, leer(r, p['frente'] + i * 4)) for i, b in enumerate(v)]
    dot = arit.suma(arit.suma(dots[0], dots[1]), dots[2])
    dot = bits(-1) if arit.racional(dot) < -1 else bits(1) if arit.racional(dot) > 1 else dot
    if s.controlado:
        tilt = s.tilt
    else:
        # Solo ejes: clasifican inequívocamente respecto a20/160 grados.
        # El valor binario de acos nativo NO forma parte de esta comparacion.
        tilt = 0 if arit.racional(dot) == 1 else bits(3.2) if arit.racional(dot) == -1 else bits(1.5)
    if analog:
        calls.update(normalizar=1, extender=2, atan2=1, truncar=1, sin=1, cos=1)
        if s.controlado:
            stickv = arit.escalar([bits(x), bits(y), 0], bits(1))
            events.append(dict(tipo='atan2_controlado', argumentos_double=[doble(stickv[1]), doble(stickv[0])],
                               retorno_double=doble(s.angulo_stick)))
        sine = s.angulo_stick if s.controlado else 0
        pitch = ag.dividir(multiplicar(vel, sine), bits(-100))
        yaw = ag.dividir(multiplicar(vel, bits(1)), bits(-100))
        if arit.racional(tilt) < arit.racional(LO) and arit.racional(pitch) > 0:
            pitch = 0
        if arit.racional(tilt) > arit.racional(HI) and arit.racional(pitch) < 0:
            pitch = 0
        requested = [(pitch, yaw, 0)]
    else:
        delta = ag.dividir(vel, bits(100))
        mask = u16(p['mando']) | u16(p['mando2'])
        requested = []
        if mask & 0x80: requested.append((0, delta, 0))
        if mask & 0x20: requested.append((0, delta ^ 0x80000000, 0))
        if mask & 0x10 and arit.racional(tilt) >= arit.racional(LO): requested.append((delta, 0, 0))
        if mask & 0x40 and arit.racional(tilt) <= arit.racional(HI): requested.append((delta ^ 0x80000000, 0, 0))
    if s.controlado:
        events.append(dict(tipo='acos_controlado', argumento_double=doble(dot), retorno_double=doble(s.tilt)))
    for xyz in requested:
        events.append(dict(tipo='rotacion_solicitada', destino=base.CAM + 0x80, fargs_bits=list(xyz)))
    if requested:
        calls.update(solicitud=len(requested), rotacion=len(requested))
    return r, dict(eventos=events, llamadas=dict(calls))


def comprobar(region, elf):
    s, filas, samples, continuos, negativos = OrbitaViewer(region, elf), [], {}, [], []
    cobertura, ramas = Counter(), Counter()
    def revisar(nombre, want, got):
        f = dict(caso=nombre, igual=want == got, esperado=want, obtenido=got)
        if not f['igual'] and all(x['igual'] for x in filas): print('Primer DIF', region, f)
        filas.append(f)
    def ejecutar(nombre, ref, **entrada):
        s.entradas(ref, **entrada)
        ref, want = modelo(s, ref)
        got = s.paso_orbita()
        revisar(nombre, want, got)
        revisar(nombre + ':memoria_abi_guardias', True, s.integridad(ref))
        cobertura.update({s.roles[a]: n for a, n in s.cpu.llamadas.items()})
        dominio = 'controlado' if s.controlado else 'nativo_ejes'
        ramas[dominio] += 1
        ramas['analogico' if got['llamadas'].get('atan2') else 'digital'] += 1
        samples.setdefault(nombre.split(':')[0], dict(esperado=want, obtenido=got))
        return ref
    nativos = controlados = 0
    for h, eje, mando, distrib in itertools.product((0, 2, 3, 4, ee.M32), EJES, range(0, 256, 16), range(3)):
        ref = s.preparar_orbita(hit=h, eje=eje)
        m1, m2 = (mando, 0) if distrib == 0 else (0, mando) if distrib == 1 else (mando, mando ^ 0xF0)
        ref = ejecutar(f'digital_nativo:{h}:{eje}:{mando}:{distrib}', ref, mando=m1, mando2=m2)
        nativos += 1
    for h, x, eje in itertools.product((0, 2, 3, ee.M32), (17, 32, 64, 127), EJES):
        ref = s.preparar_orbita(hit=h, eje=eje)
        ejecutar(f'analogico_nativo:{h}:{x}:{eje}', ref, mando=0xF0, mando2=0xF0, stick=(x, 0))
        nativos += 1
    for h, tilt, mando, distrib in itertools.product((0, 2, 3, ee.M32), ANGULOS, range(0, 256, 16), range(3)):
        ref = s.preparar_orbita(hit=h, controlado=True, tilt=tilt)
        m1, m2 = (mando, 0) if distrib == 0 else (0, mando) if distrib == 1 else (mando, mando ^ 0xF0)
        ejecutar(f'digital_frontera:{h}:{tilt:08x}:{mando}:{distrib}', ref, mando=m1, mando2=m2)
        controlados += 1
    sticks = ((-128, 0), (-127, 0), (-17, 0), (-16, 0), (-15, 0), (0, 0), (15, 0), (16, 0),
              (17, 0), (127, 0), (0, -128), (0, -127), (0, -17), (0, -16), (0, -15),
              (0, 15), (0, 16), (0, 17), (0, 127),
              (-128, 127), (16, -16), (-17, 17), (17, 16))
    for dz, stick, tilt, angulo in itertools.product((0, 16, 127), sticks, ANGULOS, TINY):
        ref = s.preparar_orbita(hit=3, deadzone=dz, controlado=True, tilt=tilt, angulo_stick=angulo)
        ejecutar(f'deadzone:{dz}:{stick}:{tilt:08x}:{angulo:08x}', ref,
                 mando=0xF0, mando2=0x10, stick=stick)
        controlados += 1
    for h, paso, mask in itertools.product((0x7FFFFFFF, 0x80000000), pose.PASOS, (0x0F0F, 0xFF00, 0xFFFF)):
        ref = s.preparar_orbita(hit=h, paso=paso)
        ejecutar(f'bits_altos_paso:{h}:{paso:08x}:{mask:04x}', ref, mando=mask, mando2=mask)
        nativos += 1
    # Secuencia de peticiones: conserva memoria; el banco cambia entradas, no
    # aplica rotaciones a la camara. El esperado nunca adopta resultados.
    for controlado, h, tilt in itertools.product((False, True), (2, 3), (LO, HI)):
        ref = s.preparar_orbita(hit=h, controlado=controlado, tilt=tilt if controlado else 0)
        pedidos = esperados = 0
        for i in range(96):
            stick = (17, 0) if i % 3 == 0 else (0, 0)
            ent = dict(mando=(i % 16) * 16, mando2=((i * 7) % 16) * 16, stick=stick)
            s.entradas(ref, **ent)
            esperados += sum(e['tipo'] == 'rotacion_solicitada' for e in modelo(s, ref)[1]['eventos'])
            ref = ejecutar(f'continuo:{int(controlado)}:{h}:{tilt:08x}:{i}', ref,
                           **ent)
            pedidos += sum(e['tipo'] == 'rotacion_solicitada' for e in s.eventos)
        revisar(f'continuo_total:{int(controlado)}:{h}:{tilt:08x}', esperados, pedidos)
        continuos.append(dict(controlado=controlado, hit=h, tilt_bits=tilt, cuadros=96,
                              peticiones_esperadas=esperados, peticiones_obtenidas=pedidos))
    for nombre, fn in (
            ('hit_bool', lambda r: s.preparar_orbita(hit=True)),
            ('deadzone_negativa', lambda r: s.preparar_orbita(deadzone=-1)),
            ('eje_no_revisado', lambda r: s.preparar_orbita(eje=(1, 0, 0))),
            ('tilt_no_revisado', lambda r: s.preparar_orbita(controlado=True, tilt=0x7FC00000)),
            ('stick_fuera', lambda r: s.entradas(r, stick=(128, 0))),
            ('stick_bool', lambda r: s.entradas(r, stick=(True, 0))),
            ('mando_fuera', lambda r: s.entradas(r, mando=0x10000)),
            ('analogico_nativo_no_revisado', lambda r: s.entradas(r, stick=(0, 17)))):
        ref = s.preparar_orbita()
        antes = base.huella(s.cpu.m)
        try: fn(ref); rechazo = False
        except ValueError: rechazo = True
        revisar('rechazo:' + nombre, True, rechazo)
        revisar('rechazo:' + nombre + ':sin_mutacion', antes, base.huella(s.cpu.m))
    controles = ((0x2C, 0x28410004, dict(hit=3), dict(mando=0x80), 'umbral_hit3'),
                 (0x2C0, 0x30630020, dict(), dict(mando=0x80), 'mascara80'),
                 (0x364, 0x45000005, dict(controlado=True, tilt=LO), dict(mando=0x10), 'igualdad_pitch'))
    for off, malo, prep, ent, nombre in controles:
        addr = s.p['orbita'] + off
        original = s.cpu.r32(addr)
        ref = s.preparar_orbita(**prep)
        s.entradas(ref, **ent)
        want_ref, want = modelo(s, ref)
        revisar('sensibilidad:' + nombre + ':original', want, s.paso_orbita())
        try:
            s.cpu.w32(addr, malo); s.cpu.code.pop(addr, None)
            mutado = s.paso_orbita()
            detectado = want != mutado or not s.integridad(want_ref)
            revisar('sensibilidad:' + nombre + ':detectada', True, detectado)
            negativos.append(dict(nombre=nombre, direccion=addr, original=original, mutado=malo,
                                  detectado=detectado, esperado=want, obtenido_mutado=mutado))
        finally:
            s.cpu.w32(addr, original); s.cpu.code.pop(addr, None)
        ejecutar('restaurado:' + nombre, ref, **ent)
    for (a, b), sha in zip(s.intervalos, s.hashes):
        revisar(f'codigo_intacto:{a:08x}:{b:08x}', sha, base.huella(s.cpu.m[a:b]))
    primera = next((f for f in filas if not f['igual']), None)
    iguales = sum(f['igual'] for f in filas)
    print(f'Orbita Viewer {region}: {iguales}/{len(filas)}, primer DIF {primera}')
    return dict(region=region, elf_sha256=s.digest, perfil=s.p, iguales=iguales, total=len(filas),
                primera_diferencia=primera, correcto=primera is None, casos=filas, muestras=samples,
                aislados_nativos=nativos, aislados_controlados=controlados, continuos=continuos,
                controles_negativos=negativos,
                ramas=dict(ramas), cobertura_llamadas=dict(cobertura),
                intervalos_codigo=[dict(inicio=a, fin_exclusivo=b, sha256=h) for (a, b), h in zip(s.intervalos, s.hashes)])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--elf-eu', type=Path, default=banco.ELF_EU)
    p.add_argument('--elf-us', type=Path, default=banco.ELF_US)
    p.add_argument('--salida', type=Path, required=True)
    a, start = p.parse_args(), time.monotonic()
    try:
        if a.salida.exists() or not a.salida.parent.is_dir():
            raise ValueError('salida nueva con padre existente requerida')
        rs = [comprobar(r, e) for r, e in (('eu', a.elf_eu), ('us', a.elf_us))]
        out = dict(operacion='viewer_orbita_decisiones', regiones=rs, pares_dorado_real=0, integrado_cpp=False,
                   referencia_continua='memoria propia; sin adoptar salidas ni aplicar rotacion a camara',
                   FPU_HW=sorted(ee.FPU_HW), segundos_banco=round(time.monotonic() - start, 3),
                   limites=['nativo: digital sobre tres ejes, analogico solo +X/y0; sin acos binario comparado',
                            'fronteras: acos/atan2 controlados, sin acreditar geometria/trig general',
                            'sin/cos originales solo angulos diminutos; normalizacion y dot originales',
                            'rotacion final solo peticion; continuos de solicitudes, no trayectoria ni gameplay',
                            'sin enlace Input/Update/poses ni C++/dorado/hardware nuevos'])
        with a.salida.open('x', encoding='utf-8') as f:
            json.dump(out, f, indent=2, ensure_ascii=False, allow_nan=False)
        return 0 if all(r['correcto'] for r in rs) else 1
    except (ValueError, OSError, RuntimeError, NotImplementedError) as exc:
        print('FALLO:', exc)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
