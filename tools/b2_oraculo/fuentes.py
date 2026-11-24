"""Sello de los fragmentos C++ revisados, independiente del resto del archivo."""
import hashlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SECTIONS = {
    'aliados': ('src/pasarela_b2.cpp', 'void Preparar() {', '\nvoid Guion(int k, int prof);'),
    'personajes': ('src/pasarela_b2.cpp', 'constexpr int kB2aB3[', '\n// Nombre del registro'),
    'leer_piezas': ('src/pasarela_b2.cpp', '  for (uint32_t p = 1; p < n2;', '  // DS23: la tabla'),
    'activa': ('src/pasarela_b2.cpp', 'bool Activa(const Ficha& f)', '\n'),
    'inicio_piezas': ('src/pasarela_b2.cpp', 'void Empezar()', '  // EU 0x27BD80'),
    'fase': ('src/pasarela_b2.cpp', '    if (o[1] == 41 || o[1] == 42 || o[1] == 43)',
             '    // EU 0x2938A0'),
    'rivales': ('src/pasarela_b2.cpp', 'int PasoAlAzar(int desde)', '\n// EU 0x235E70'),
    'orden_rivales': ('src/pasarela_b2.cpp', 'void FaseRival()', '\n// Ordenes del guion que cambian'),
    'recarga_rivales': ('src/pasarela_b2.cpp', 'void TurnoRival()', '\nvoid TurnoJugador()'),
    'ajuste_inventario': ('src/pasarela_b2.cpp', 'REXCVAR_DEFINE_BOOL(dbz3_b2_requisitos_inventario,', '\n',),
    'capsulas': ('src/pasarela_nave.cpp', 'int CapsulaAlAzar(int grupo, int personaje_b2)',
                '\nbool CapsulaNueva'),
    'deseos': ('src/pasarela_b2_shenron.inl', 'struct DeseoB2', '\nvoid DarCapsulaB3'),
}

# Se completa una sola vez tras revisar los fragmentos; una modificacion exige
# revisar modelo_port.py. Nunca se regenera silenciosamente durante las pruebas.
REVIEWED = {
    'aliados': '4c0e7f34a266639628329234a76de89691cbef45723dbf4027506631557aa4e1',
    'personajes': '278ed5d9ac783b53d2b33522d5d5a0fd25087e941bb26f02155f13ac0de16d2d',
    'leer_piezas': '6113457bb79d1fda13361029f0c9fc8b211131a9dce6dc6c65aed574bb44a9ca',
    'activa': '651b8f97270419f162a49f95220084c05fa9ad514e96330ee28957ac782ef2fc',
    'inicio_piezas': 'b272e01074973b13855612903aca150d73fbba93bde1f5c6755aec45d04706dc',
    'fase': 'f27a7904f914f7d853c4d9eaf60fd6e1dcba8cde35381949b52374eb8517bf7d',
    'rivales': '599a89446759fe5b816f78222932c281193eeca28cf52615318e7257140a5644',
    'orden_rivales': '0b0ac3274dc7c305b51341d6a3e5fdd2dc188175588865f9a127692ef1e71dc9',
    'recarga_rivales': 'b6316fc3cc6f40a6f8a4141489aba4958b33bbe952e67f48efbfc1ff150c93d3',
    'ajuste_inventario': 'f118264a101e34a6586f4b770672824afd34c68a45ea2fce47265b9406466377',
    'capsulas': 'f3728760f3e1841ccee3874c5fe91f42841305cb7d25d730cf5f135c60f79fd7',
    'deseos': '2362693eb8ebc907844f40971e9fa73274c2f32bb8e159b79de42852f1d11425',
}


def observe():
    result = {}
    for key, (file, start, end) in SECTIONS.items():
        path = ROOT / file
        raw = path.read_bytes()
        text = raw.decode('utf-8-sig').replace('\r\n', '\n')
        lo = text.index(start)
        hi = text.index(end, lo)
        result[key] = dict(archivo=file, linea=text[:lo].count('\n') + 1,
                           sha256_fragmento=hashlib.sha256(text[lo:hi].encode()).hexdigest(),
                           sha256_archivo=hashlib.sha256(raw).hexdigest())
    return result


def validate():
    observed = observe()
    stale = [key for key, row in observed.items() if REVIEWED.get(key) != row['sha256_fragmento']]
    if stale:
        detalle = ', '.join(f"{k}={observed[k]['sha256_fragmento']}" for k in stale)
        raise ValueError('Modelo pendiente de revision por cambios en fuentes: ' + detalle)
    return observed


if __name__ == '__main__':
    import json
    print(json.dumps({k: v['sha256_fragmento'] for k, v in observe().items()}, indent=2))
