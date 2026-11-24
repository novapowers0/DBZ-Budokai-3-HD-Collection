# SPDX-License-Identifier: MIT
"""F1: enlace offline estado(Input incluye orbita) -> Viewer_Update EU/USA.

Orden de invocacion elegido explicitamente: no es el scheduler completo del
juego. Rotacion, poses, UI, premios y servicios Update siguen sin efectos reales.
"""
import argparse
import json
from pathlib import Path
import time

import disparo_viewer_input_orbita as enlace
import disparo_viewer_update as actual
import ee

base, bits, leer, escribir = enlace.base, enlace.bits, enlace.leer, enlace.escribir


class CuadroViewer(enlace.InputOrbitaViewer, actual.UpdateViewer):
    def __init__(self, region, elf=None):
        # MRO cooperativa: Update prepara sus servicios, Input y sus bases una
        # sola vez; InputOrbita quita despues SOLO el puente de orbita.
        super().__init__(region, elf)
        for a, rol in zip(self.jal['viewer_update'][-5:], actual.SERVICIOS):
            self.nombres[a] = rol

    def preparar_enlace(self, dificultad=0, banderas=(1, 1, 1, 1, 1), **kw):
        if (type(banderas) not in (tuple, list) or len(banderas) != 5
                or any(type(x) is not int or not 0 <= x <= ee.M32 for x in banderas)):
            raise ValueError('cinco banderas u32 requeridas')
        ref = super().preparar_enlace(dificultad, **kw)
        for off, valor in zip(actual.HITOS, banderas):
            self.cpu.w32(actual.CTX + off, valor)
            escribir(ref, actual.CTX + off, valor)
        a = self.p['umbral25']
        ref[a] = bytearray(self.cpu.m[a:a + 4])
        return ref

    def paso_enlace(self, **kw):
        self._validar_update()
        super().paso_enlace(**kw)
        # Se ejecuta despues de retornar del estado: nunca call() anidada desde
        # un hook. Los eventos acumulados mantienen el orden de ambas fases.
        obs = self._actualizar()
        c, p = self.cpu, self.p
        if any(a not in self.nombres for a in c.llamadas):
            raise RuntimeError('llamada no inventariada en el cuadro enlazado')
        obs['llamadas_enlace'] = {k: c.llamadas.get(a, 0) for k, a in (
            ('orbita', p['orbita']), ('solicitud', p['solicitud']),
            ('rotacion', 0x127160), ('atan2', 0x1177C8), ('sin', 0x117688), ('cos', 0x117438))}
        return obs


def modelo(s, ref, **kw):
    r, estado = enlace.modelo(s, ref, **kw)
    r, obs = actual.modelo(s, r)
    obs['eventos'] = estado['eventos'] + obs['eventos']
    obs['llamadas_enlace'] = estado['llamadas_enlace']
    return r, obs


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--elf-eu', type=Path, default=enlace.orb.banco.ELF_EU)
    p.add_argument('--elf-us', type=Path, default=enlace.orb.banco.ELF_US)
    p.add_argument('--salida', type=Path, required=True)
    a, start = p.parse_args(), time.monotonic()
    try:
        if a.salida.exists() or not a.salida.parent.is_dir():
            raise ValueError('salida nueva con padre existente requerida')
        rs = []
        for region, elf in (('eu', a.elf_eu), ('us', a.elf_us)):
            # Una mutacion nueva en Update prueba que su observacion tambien
            # forma parte de la comparacion conjunta, no solo la de Input.
            direccion = actual.PERFILES[region][0] + 0xE0
            rs.append(enlace.comprobar(region, elf, clase=CuadroViewer, referencia=modelo,
                      mutaciones_extra=(('update_sonido12_a11', direccion, 0x2404000B, {}),)))
        out = dict(operacion='viewer_input_orbita_update', regiones=rs,
                   orden='estado(Input incluye orbita) -> Viewer_Update; elegido, no scheduler completo',
                   pares_dorado_real=0, integrado_cpp=False, FPU_HW=sorted(ee.FPU_HW),
                   referencia_continua='memoria propia sin adoptar salidas; mismo corpus del enlace Input/orbita',
                   eventos_por_caso='SHA256 del payload completo y orden; muestras completas seleccionadas',
                   segundos_banco=round(time.monotonic() - start, 3),
                   limites=['digital sobre tres ejes y analogico +X/y0; sin acos binario comparado',
                            'rotacion/poses/UI/premios y servicios Update solo peticiones sin efectos reales',
                            'hits/acumulador/impacto/camara/timer externos, no trayectoria ni colision reales',
                            'orden de banco, no scheduler de tareas/poses ni C++/dorado/hardware nuevos'])
        with a.salida.open('x', encoding='utf8') as f:
            json.dump(out, f, indent=2, ensure_ascii=False, allow_nan=False)
        return 0 if all(r['correcto'] for r in rs) else 1
    except (ValueError, RuntimeError, OSError, NotImplementedError) as exc:
        print('FALLO:', exc)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
