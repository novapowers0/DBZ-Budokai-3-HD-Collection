"""Bocetos bidimensionales de autor; ejecutar ANTES del modelado Blender."""
from pathlib import Path
from html import escape
import math

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'studies 5/ENTREGAS/R16'
NOMBRES = {
    'cabezas': ('Semilla', 'Aguja', 'Bastión', 'Estela', 'Cáliz', 'Guijarro', 'Órbita', 'Duna'),
    'antenas': ('Espiral', 'Brote', 'Tallo', 'Gemelas', 'Viento', 'Sauce'),
    'orejas': ('Lanza', 'Remo', 'Hoja', 'Muesca'),
    'mandibulas': ('Serena', 'Firme', 'Proa', 'Velo'),
    'parches': ('Costillas', 'Rombos', 'Canales'),
    'ropa': ('Surco', 'Nómada', 'Umbral', 'Estrato'),
}


def dibujar(slot, k, perfil):
    stroke = 'fill="#94b786" stroke="#233832" stroke-width="2.5" stroke-linejoin="round"'
    def path(d, fill=None):
        return '<path d="%s" %s%s/>' % (d, stroke, '' if fill is None else ' style="fill:%s"'%fill)
    if slot == 'cabezas':
        tops = (44, 20, 48, 33, 29, 57, 22, 40)
        widths = (34, 26, 43, 31, 45, 35, 30, 36)
        w, y = widths[k], tops[k]
        if perfil:
            back = (110, 103, 114, 133, 119, 109, 115, 126)[k]
            return path(f'M 77 165 L 77 142 L 61 126 L 60 109 L 51 102 L 61 94 L 60 72 Q 55 {y} 85 {y} Q {back} {y} {back} 85 L 110 128 L 94 144 L 94 165 Z')
        crown = f'Q 85 {y-10} {85+w} 67'
        if k == 2: crown = f'L {85-w} {y} L {85+w} {y} L {85+w} 67'
        if k == 4: crown = f'L 58 {y} L 85 {y+20} L 112 {y} L {85+w} 67'
        return path(f'M 73 165 L 73 143 L 58 128 L {85-w} 67 {crown} L 111 128 L 96 143 L 96 165 Z') + path(f'M 59 91 L 77 {85+k%3*5} M 111 91 L 93 {85+k%3*5}', 'none') + path(f'M 81 {y+14} L 79 69 M 89 {y+12} L 91 68', 'none')
    if slot == 'antenas':
        curves = (
            'M 74 110 C 40 33 20 48 35 68 C 50 91 64 49 40 51',
            'M 74 110 Q 60 82 67 69',
            'M 74 110 Q 35 71 39 36',
            'M 74 110 Q 56 64 46 33 M 68 91 Q 88 50 71 32',
            'M 74 110 C 55 74 28 70 16 91' if perfil else 'M 74 110 Q 47 64 39 96',
            'M 74 110 C 40 52 14 102 24 147',
        )
        d = curves[k]
        return '<path d="%s" fill="none" stroke="#548762" stroke-width="%d" stroke-linecap="round"/>'%(d, 12 if k == 2 else 6) + ('' if perfil else '<g transform="translate(170 0) scale(-1 1)"><path d="%s" fill="none" stroke="#548762" stroke-width="%d" stroke-linecap="round"/></g>'%(d, 12 if k == 2 else 6))
    if slot == 'orejas':
        points = ('60,113 62,69 140,40 113,103 66,128', '60,113 65,74 145,83 119,118 66,128', '60,113 63,81 101,70 111,98 66,128', '60,113 63,70 142,42 117,75 125,86 105,94 67,129')[k]
        if perfil: points = ('65,119 68,72 141,58 117,110', '65,119 68,78 151,111 125,128', '65,119 69,79 107,91 98,120', '65,119 68,72 142,58 125,84 129,96 111,96')[k]
        return '<polygon points="%s" %s/>'%(points,stroke)
    if slot == 'mandibulas':
        w = (23, 37, 26, 36)[k]
        d = f'M {85-w} 78 L {85+w} 78 L {85+w-4} 120 L 97 140 L 73 140 L {85-w+4} 120 Z'
        if perfil: d = f'M 70 78 L 97 78 L {112 if k == 2 else 100} 132 L 83 142 L 71 126 Z'
        return path(d, '#526b7c' if k == 3 else None) + path('M 62 103 Q 85 111 108 103 M 65 117 Q 85 126 105 117' if k == 3 else 'M 72 112 L 98 112', 'none')
    if slot == 'parches':
        d = path('M 65 40 Q 49 91 66 150 L 105 150 Q 119 90 104 40 Z')
        for j in range(4 if k == 0 else 3 if k == 1 else 2):
            if k == 0: d += path(f'M 66 {62+j*21} L 105 {62+j*21} L 101 {71+j*21} L 67 {71+j*21} Z', '#c77f83')
            elif k == 1: d += path(f'M 85 {48+j*31} L 106 {65+j*31} L 85 {80+j*31} L 65 {65+j*31} Z', '#c77f83')
            else: d += path(f'M {66+j*23} 52 L {80+j*23} 52 L {80+j*23} 138 L {66+j*23} 138 Z', '#c77f83')
        return d
    d = path('M 72 37 L 99 37 L 117 62 L 110 117 L 111 157 L 87 157 L 85 124 L 82 157 L 58 157 L 61 117 L 54 62 Z', '#526b7c')
    if k == 0: d += path('M 62 48 L 99 105 L 107 102 L 71 44 Z M 59 105 L 111 105 L 111 119 L 59 119 Z M 96 118 L 106 118 L 114 151 L 100 147 Z', '#c3a568')
    elif k == 1: d += path('M 62 38 Q 85 60 108 38 L 109 59 L 97 66 L 99 104 L 82 102 L 80 62 L 62 58 Z', '#c3a568')
    elif k == 2: d += path('M 55 57 Q 47 11 85 14 Q 123 11 115 57 L 103 63 L 100 44 Q 85 29 69 44 L 67 63 Z M 56 66 L 73 66 L 76 137 L 54 133 Z M 96 66 L 114 66 L 116 133 L 94 137 Z')
    else:
        for j in range(3): d += path(f'M 60 {57+j*17} L 110 {57+j*17} L 104 {69+j*17} L 67 {69+j*17} Z', '#aaa77b')
    return d


def main():
    for slot, names in NOMBRES.items():
        width = 190 * len(names)
        parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="485" viewBox="0 0 {width} 485">',
                 f'<rect width="{width}" height="485" fill="#f3f0e7"/>',
                 f'<text x="14" y="27" font-family="sans-serif" font-size="19">NMC · {slot} · bocetos originales, frente / perfil</text>']
        for k, name in enumerate(names):
            parts.append(f'<g transform="translate({k*190+10} 45)">')
            parts.append(f'<text x="5" y="15" font-family="sans-serif" font-size="16">Tipo {k+1} · {escape(name)}</text>')
            for profile, dy in ((False, 20), (True, 222)):
                parts.append(f'<g transform="translate(0 {dy})">{dibujar(slot,k,profile)}</g>')
            parts.append('</g>')
        parts.append('</svg>')
        path = OUT / ('NMC_'+slot+'.svg')
        with path.open('x', encoding='utf-8') as f:
            f.write('\n'.join(parts))
        print('PASS diseño', slot, len(names), 'tipos frente/perfil')


if __name__ == '__main__':
    main()
