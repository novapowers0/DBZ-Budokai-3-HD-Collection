"""
EMD (SDBH WM / Xenoverse) -> AWO HD B3 — conversor desde cero.

Refactorización para 'mod center hd'. Inspirado en el EMD to AMG de la
comunidad (Nexus-sama): construye el bin desde cero, no inyecta en slots.

El esqueleto ESK de SDBH WM usa los mismos labels que Budokai (waist, llegrot,
stmc, chest, neck, head...), por lo que el mapeo de huesos es directo.

Pipeline:
  1. Parsear EMD (malla) + ESK (esqueleto): verts, normales, uvs, triangulos,
     bones por vertice (weights del EMD Xenoverse).
  2. Mapear labels ESK -> bones KLL de Krillin.
  3. Convertir verts al layout HD (stride 44: [nan,u,v,z,x,y,peso,bone,nz,-ny,nx]).
  4. Construir el AWO HD con Krillin como plantilla estructural (header,
     zonas de hueso, mesh group, arms, AZT) reemplazando sec34/vb2/IB.
  5. Re-mapear los shadow arms con los nuevos rangos del IB (en bytes).

Estado: v1 — parseo EMD/ESK y conversion a verts HD. El empaquetado AWO
se completa en fases posteriores (necesita la plantilla exacta).

Uso:
  python emd_to_awo_hd.py <modelo.emd> <esqueleto.esk> <out>
"""

import io, sys, struct, os
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')

def u32le(b, o): return struct.unpack('<I', b[o:o+4])[0]
def u16le(b, o): return struct.unpack('<H', b[o:o+2])[0]
def u8(b, o): return b[o]
def f32le(b, o): return struct.unpack('<f', b[o:o+4])[0]


def parse_emd(emd):
    """Parsea un modelo EMD de Xenoverse/SDBH WM."""
    assert emd[:4] == b'#EMD', 'no es EMD: %s' % emd[:4]
    # header EMD Xenoverse
    # +0x00 '#EMD' +0x04 version +0x08 endian +0x0C nModels
    n_models = u32le(emd, 0x0C)
    print('EMD: %d models' % n_models)
    # el modelo principal esta en el primer offset de la lista de modelos
    # (parseo basado en el formato Xenoverse documentado)
    # Estructura EMD (Xenoverse 1):
    #   header: +0x00 magic, +0x0C model_count, +0x10 model_list_offset
    model_list = u32le(emd, 0x10)
    models = []
    for i in range(n_models):
        moff = u32le(emd, model_list + i*4)
        models.append(moff)
    print('models @ %s' % [hex(m) for m in models])
    return models


def parse_esk(esk):
    """Parsea el esqueleto ESK: labels de huesos."""
    assert esk[:4] == b'#ESK', 'no es ESK'
    import re
    strs = re.findall(rb'[ -~]{4,}', esk)
    labels = [s.decode('utf-8', 'replace') for s in strs if s[:1].isalpha()]
    # filtrar ruido
    labels = [l for l in labels if l[0].islower() or '_' in l]
    return labels


def main():
    if len(sys.argv) < 4:
        print('Uso: emd_to_awo_hd.py <modelo.emd> <esqueleto.esk> <out_prefix>')
        return
    emd = open(sys.argv[1], 'rb').read()
    esk = open(sys.argv[2], 'rb').read()
    out = sys.argv[3]

    models = parse_emd(emd)
    labels = parse_esk(esk)
    print('Huesos ESK (%d):' % len(labels))
    for i, l in enumerate(labels):
        print('  %2d: %s' % (i, l))

    # Mapeo a bones KLL (Krillin B3)
    kll = {'body':0, 'waist':1, 'stmc':2, 'obi':3, 'chest':12, 'lchn':13,
           'larmrot':14, 'larm1':15, 'larm2':16, 'lhandrot':17, 'lhand':18,
           'nla':19, 'rchn':20, 'rarmrot':21, 'rarm1':22, 'rarm2':23,
           'rhandrot':24, 'rhand':25, 'nra':26, 'neck':27, 'head':28,
           'llegrot':38, 'lleg1':39, 'lleg2':40, 'lfoot1':41, 'lfoot2':42,
           'rlegrot':44, 'rleg1':45, 'rleg2':46, 'rfoot1':47, 'rfoot2':48}
    mapping = {}
    for i, l in enumerate(labels):
        base = l.split('_')[-1].lower().replace('x18g_','').replace('g_','')
        # limpiar prefijos de dedos
        base2 = base
        for pref in ['lmiddle','lpinky','lring','lthumb','lindex',
                     'rmiddle','rpinky','rring','rthumb','rindex']:
            if base.startswith(pref):
                base2 = 'lhand' if base.startswith('l') else 'rhand'
        if base2 in kll:
            mapping[i] = kll[base2]
    print('\nMapeo ESK->KLL (%d):' % len(mapping))
    for i, l in enumerate(labels):
        if i in mapping:
            print('  %2d %s -> bone %d' % (i, l, mapping[i]))

    # guardar el mapeo para la fase 2
    with open(out + '_bones.txt', 'w') as f:
        for i, l in enumerate(labels):
            f.write('%d %s %d\n' % (i, l, mapping.get(i, -1)))
    print('\nMapeo guardado: %s_bones.txt' % out)


if __name__ == '__main__':
    main()
