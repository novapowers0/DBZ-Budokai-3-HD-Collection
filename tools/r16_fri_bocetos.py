"""Láminas SVG previas: siluetas propias, frente y perfil; no lee imágenes."""
from pathlib import Path
from html import escape

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'studies 5/ENTREGAS/R16'
NAMES = {
    'cabeza': ('Orbe', 'Estela', 'Velamen', 'Bifurca', 'Estrato', 'Visera', 'Martillo', 'Aguja'),
    'cuernos': ('Horizonte', 'Ascua', 'Ráfaga', 'Averno', 'Brotes', 'Despejado'),
    'cara': ('Cuña', 'Respirador', 'Bastión', 'Lúnula'),
    'placas': ('Arco', 'Vértice', 'Escama', 'Baluarte'),
    'gema': ('Lente', 'Rombo', 'Tríada'),
    'cola': ('Látigo', 'Quilla', 'Maza'),
}
# Cada par se dibuja desde cero. Coordenadas locales de boceto, no calcos.
PATHS = {
'cabeza': [
('M-28 0 Q-35-52 0-58 Q35-52 28 0Z', 'M-24 0 Q-36-50 0-58 Q35-48 24 0Z'),
('M-30 0 L-33-36 Q0-58 33-36 L30 0Z', 'M-25 0 L-62-26 L-72-48 Q0-65 26-32 L27 0Z'),
('M-27 0 L-26-38 L-8-45 L0-82 L8-45 L26-38 L27 0Z', 'M-25 0 L-44-25 L-45-72 L-5-82 L15-42 L27 0Z'),
('M-28 0 L-30-65 L-16-76 L-9-40 L9-40 L16-76 L30-65 L28 0Z', 'M-24 0 L-47-30 L-39-76 L-8-64 L22-32 L25 0Z'),
('M-29 0 L-33-27 L-29-32 L-32-39 L-25-44 L-21-52 L0-59 L21-52 L25-44 L32-39 L29-32 L33-27 L29 0Z', 'M-25 0 L-34-24 L-28-29 L-35-35 L-26-39 L-27-47 L-10-58 L15-53 L23-38 L32-30 L25 0Z'),
('M-26 0 L-40-28 L-28-51 L0-59 L28-51 L40-28 L26 0Z', 'M-24 0 L-32-30 L-18-55 L15-51 L41-26 L25-20 L25 0Z'),
('M-26 0 L-48-20 L-49-39 L-25-49 L25-49 L49-39 L48-20 L26 0Z', 'M-24 0 L-26-38 L-10-49 L20-49 L32-25 L25 0Z'),
('M-27 0 L-26-38 L0-88 L26-38 L27 0Z', 'M-24 0 L-41-42 L-15-87 L18-44 L25 0Z')],
'cuernos': [
('M-23 0 L-78-17 L-25-12Z M23 0 L78-17 L25-12Z', 'M-5 0 L25-12 L0-13Z'),
('M-24 0 Q-67-42-33-77 Q-43-27-20-12Z M24 0 Q67-42 33-77 Q43-27 20-12Z','M-6 0 Q-30-50 1-75 Q-12-30 7-10Z'),
('M-24 0 L-48-34 L-30-18Z M24 0 L48-34 L30-18Z','M-7 0 Q-65-12-76-54 Q-33-26 8-10Z'),
('M-23 0 Q-83-10-63-66 Q-56-86-29-96 Q-58-54-35-22Z M23 0 Q83-10 63-66 Q56-86 29-96 Q58-54 35-22Z','M-8 0 Q-44-32-23-69 L0-96 Q-13-46 9-13Z'),
('M-24 0 L-37-13 L-34-23 L-25-24 L-18-11Z M24 0 L37-13 L34-23 L25-24 L18-11Z','M-8 0 L-15-20 L0-24 L9-5Z'),
('', '')],
'cara': [
('M-27-23 L27-23 L14 8 L0 19 L-14 8Z','M-13-24 L14-24 L20 5 L4 18 L-14 9Z'),
('M-26-24 L26-24 L20 16 L-20 16Z M-17-12H17 M-14-2H14 M-11 8H11','M-12-23 L18-23 L26 11 L-12 16Z'),
('M-36-30 L-21-37 L-14-17 L14-17 L21-37 L36-30 L27 19 L4 19 L0 13 L-4 19 L-27 19Z','M-20-30 L0-37 L24-20 L27 16 L-17 19Z'),
('M-30-35 Q-8-24-19 1 L-10 12 Q0 24 10 12 L19 1 Q8-24 30-35 L35-19 L23 12 Q0 32-23 12 L-35-19Z','M-16-29 Q15-39 17-20 L10-6 Q25 20 4 24 L-14 9Z')],
'placas': [
('M-60-33 Q-45-56-29-29 Q0-41 29-29 Q45-56 60-33 L45-17 L29-18 Q0 1-29-18 L-45-17Z','M-26-35 Q0-60 21-32 L34-10 L20 8 L9-15 L-25-11Z'),
('M-65-41 L-36-54 L-29-29 L0-12 L29-29 L36-54 L65-41 L48-18 L30-20 L0 10 L-30-20 L-48-18Z','M-36-47 L8-54 L29-28 L20-13 L26 13 L8-2 L-24-17Z'),
('M-59-38 L-38-48 L-27-25 L-4-29 L-4-17 L-26-13 L-4-10 L-4 1 L-26 5 L-4 9 L-4 20 L-28 21 L-34-17 L-55-16Z M59-38 L38-48 L27-25 L4-29 L4-17 L26-13 L4-10 L4 1 L26 5 L4 9 L4 20 L28 21 L34-17 L55-16Z','M-26-45 L10-48 L28-28 L14-20 L23-10 L13-1 L21 9 L0 20 L-12-17 L-28-17Z'),
('M-64-45 L-32-45 L-29-26 L0-39 L29-26 L32-45 L64-45 L64-16 L34-12 L0 19 L-34-12 L-64-16Z','M-28-47 L10-47 L29-30 L29-10 L18-10 L29 5 L5 22 L-17-20 L-28-17Z')],
'gema': [('M0-30 C28-30 28 20 0 20 C-28 20-28-30 0-30Z','M0-30 Q16-5 0 20 Q-8-5 0-30Z'),('M0-34 L24-5 L0 25 L-24-5Z','M0-34 L12-5 L0 25 L-6-5Z'),('M-16-21 a9 9 0 1 0 1 0 M16-21 a9 9 0 1 0 1 0 M0 9 a9 9 0 1 0 1 0','M0-21 a7 7 0 1 0 1 0 M0 9 a7 7 0 1 0 1 0')],
'cola': [('M-10-40 Q-22 10 0 51 Q-5 1 10-40Z','M-10-40 Q-38-7-19 33 Q0 64 23 33 Q6 46 0 30 Q-18 0 10-40Z'),('M-10-40 L-18 19 L0 58 L18 19 L10-40Z','M-10-40 Q-32-8-17 25 L-28 40 L-5 38 L23 59 L17 28 Q-15 0 10-40Z'),('M-11-40 L-13 23 Q-33 60 0 65 Q33 60 13 23 L11-40Z','M-10-40 Q-35-2-15 26 Q-35 64 0 66 Q35 63 14 29 Q-15-5 10-40Z')],
}

def main():
    for slot, names in NAMES.items():
        path = OUT / ('FRI_'+slot+'.svg')
        if path.exists():
            raise FileExistsError(path)
        rows = (len(names)+1)//2
        s = [f'<svg xmlns="http://www.w3.org/2000/svg" width="1000" height="{rows*225+65}" viewBox="0 0 1000 {rows*225+65}">',
             '<rect width="100%" height="100%" fill="#f5f0df"/>',
             f'<text x="20" y="34" font-family="sans-serif" font-size="23">FRI · {escape(slot)} · frente / perfil · diseño original</text>']
        for k, (name, pair) in enumerate(zip(names, PATHS[slot])):
            x, y = (k%2)*500, (k//2)*225+65
            s.append(f'<g transform="translate({x},{y})"><text x="20" y="25" font-family="sans-serif" font-size="18">Tipo {k+1} — {escape(name)}</text>')
            for j, p in enumerate(pair):
                s.append(f'<g transform="translate({130+j*210},145)"><path d="{p}" fill="#267c86" stroke="#18202c" stroke-width="2.3" stroke-linejoin="round"/></g>')
            if slot == 'cuernos':
                s.append('<text x="30" y="192" font-family="sans-serif" font-size="13">Raíces a ambos lados del casco; frente y perfil sin casco.</text>')
            s.append('</g>')
        s.append('</svg>')
        path.write_text('\n'.join(s), encoding='utf-8')
        print('PASS boceto', slot, len(names), path)

if __name__ == '__main__':
    main()
