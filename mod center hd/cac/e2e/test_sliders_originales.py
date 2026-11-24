"""Contrato autónomo de los controles de piezas originales."""
import sys
from pathlib import Path
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import sliders_originales as s

# Tetraedro de autor con raíz en el origen, para cada control y extremo.
p = np.array([[0, 0, 0], [.3, .3, 0], [0, .3, .3], [0, 1, 0]], float)
t = np.array([[0, 2, 1], [0, 1, 3], [1, 2, 3], [2, 0, 3]])
casos = [(r, tipo, k) for r, ks in s.CONTROLES.items() for tipo, ts in s.TIPOS.items()
         if all(x in ks for x in ts) for k in ts]
for raza, tipo, control in casos:
    resultados = [s.deformar(raza, tipo, p, t, {control: x}) for x in (0, 50, 100)]
    assert np.allclose(resultados[1][0], p)
    assert not np.allclose(resultados[0][0], resultados[2][0]), (raza, tipo, control)
    for q, n in resultados:
        assert np.isfinite(q).all() and np.allclose(np.linalg.norm(n, axis=1), 1)
        assert np.allclose(q[0], p[0])
    assert np.allclose(s.deformar(raza, tipo, p, t, {control: -500})[0], resultados[0][0])
    assert np.allclose(s.deformar(raza, tipo, p, t, {control: 500})[0], resultados[2][0])
    try:
        s.normalizar(raza, {control: float('nan')})
        raise AssertionError('NaN aceptado')
    except ValueError:
        pass
assert np.allclose(p[0], [0, 0, 0])
try:
    s.deformar('namek', 'cuerno', p, t, {})
    raise AssertionError('raza incompatible aceptada')
except ValueError:
    pass
print('PASS sliders_originales: %d controles/raza; mínimo, medio, máximo, límites y normales' % len(casos))
