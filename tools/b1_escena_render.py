"""Render offline de la caché PAL 1910 (schema 1, reproductor B1C4)."""
import argparse
from fractions import Fraction
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tomllib

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
BLENDER = Path(r'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe')


def plan(cache):
    raw = (cache / 'escena_1910.toml').read_bytes()
    doc = tomllib.loads(raw.decode('utf-8'))
    if (doc['schema'], doc['spx'], doc['amb'], doc['units_per_second']) != (1, 1910, 1909, 60):
        raise ValueError('Se requiere la caché PAL 1910, esquema 1')
    cam = doc['camaras']
    rows = [(e['clip'], e['t'], e['largo']) for e in doc['eventos'] if e['tipo'] == 'camara']
    if len(cam) != 14 or rows != [(i, c['t'], c['frames']) for i, c in enumerate(cam) if c['clip'] == i]:
        raise ValueError('Inventario de cortes SPX/AMC incoherente')
    tick = Fraction(6, 5)  # reloj PAL de 50 cuadros/s; builtin 0x120, tabla Opus
    cuts = []
    for (i, t, frames), end in zip(rows, [r[1] for r in rows[1:]] + [doc['duration_units']], strict=True):
        start, end = Fraction(str(t)), Fraction(str(end))
        frame = round((start + end) / (2 * tick))
        sample = frame * tick
        if not start <= sample < end:
            raise ValueError(f'Corte fuera de orden o sin un cuadro PAL: {i}')
        cuts.append(dict(corte=i, amc_clip=i, inicio_u=float(start), fin_exclusivo_u=float(end),
                         amc_frames=frames, cuadro_pal=frame, t_u=float(sample),
                         segundos=frame / 50, png=f'1910_corte_{i:02}.png'))
    return dict(schema='b1-tr3-v1', spx=1910, amb=1909, disco='SLES_512.33', bank='DATA_EN.AFS', idioma='EN',
                iso_sha256=doc['iso_sha256'], iso_size=doc['iso_size'],
                timeline_sha256=hashlib.sha256(raw).hexdigest(), resolucion=[640, 448],
                unidades_por_segundo=60, cuadros_pal_por_segundo=50, cortes=cuts)


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(8 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def write_json(path, content):
    with path.open('x', encoding='utf-8') as out:
        out.write(json.dumps(content, ensure_ascii=False, indent=2) + '\n')


def section(text, start, end, replacement=None):
    a = text.index(start)
    b = text.index(end, a)
    return text[a:b] if replacement is None else text[:a] + replacement + text[b:]


def take(src, name, start, end):
    return section((src / name).read_text(encoding='utf-8'), start, end)


def payload(cache, iso, times):
    # Mismo puente de E/S que el arnés B1C6: Python lee los bytes originales;
    # los lectores C++ de AMO/AMT/AMM/HMS3/SPX los interpretan sin cambios.
    sys.dont_write_bytecode = True
    sys.path.insert(0, str(ROOT / 'mod center hd'))
    from historia_b1_escenas import Afs, Iso

    bank = Afs(Iso(iso), 'DATA_EN.AFS')
    off, size = bank.entries[1909]
    chunks = {'cuadros.txt': times.encode('ascii'), 'data_en.afs:0': bank.iso.read(bank.path, 0, 8),
              'data_en.afs:8': bank.iso.read(bank.path, 8, bank.head-8), f'data_en.afs:{off}': bank.read(1909)}
    if len(chunks[f'data_en.afs:{off}']) != size:
        raise ValueError('Lectura corta de AMB 1909')
    raw = (cache / 'escena_1910.toml').read_bytes()
    chunks['escena_1910.toml'] = raw
    doc = tomllib.loads(raw.decode('utf-8'))
    names = {r['matrices'] for r in doc['retarget']} | {r['file'] for r in doc['clips'] if r['kind'] in ('amm', 'amp')}
    for a in doc['actores']:
        names.update([a['hd_skin']] if 'hd_skin' in a else [a['model'], a['texture']])
    for name in sorted(names):
        path = (cache / name).resolve()
        if path.parent != cache or path.name != name:
            raise ValueError(f'Ruta de recurso no admitida: {name}')
        chunks[name] = path.read_bytes()
    packed = bytearray(struct.pack('<I', len(chunks)))
    for name, data in chunks.items():
        key = name.encode('ascii')
        packed.extend(struct.pack('<II', len(key), len(data)))
        packed.extend(key)
        packed.extend(data)
    return packed


def extractor(work, env):
    s = ROOT / 'src'
    b1, ps2 = 'pasarela_b1.cpp', 'ps2_modelo.cpp'
    head = r'''#include "cinematica.h"
#include <rex/logging.h>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <fcntl.h>
#include <io.h>
static std::unordered_map<std::string,std::vector<uint8_t>> blobs;
static bool CargarFixture() {
  if (_setmode(_fileno(stdin),_O_BINARY)==-1) return false;
  uint32_t n;
  if (std::fread(&n,4,1,stdin)!=1 || n>512) return false;
  for (uint32_t i=0;i<n;++i) {
    uint32_t lens[2];
    if (std::fread(lens,4,2,stdin)!=2 || lens[0]>256 || lens[1]>(128u<<20)) return false;
    std::string key(lens[0],'\0');
    if (std::fread(key.data(),1,key.size(),stdin)!=key.size()) return false;
    auto& bytes=blobs[key]; bytes.resize(lens[1]);
    if (std::fread(bytes.data(),1,bytes.size(),stdin)!=bytes.size()) return false;
  }
  return true;
}
static std::vector<uint8_t> Fixture(const std::string& key) {
  const auto it=blobs.find(key);
  return it==blobs.end() ? std::vector<uint8_t>{} : it->second;
}
#undef REXLOG_WARN
#undef REXLOG_INFO
#define REXLOG_WARN(fmt, x) (std::cerr << x << '\n')
#define REXLOG_INFO(...) ((void)0)
namespace dbz3::settings { int Language() { return 1; } }
#include "tr3_scene.inl"
'''
    scene = (s / 'escena_b1_1910.inl').read_text(encoding='utf-8')
    scene = section(scene, '  std::ifstream f(dir / nombre,', '  return out;',
                    '  auto out=Fixture(nombre);\n  Exigir(!out.empty() && out.size()<=tope,"cache fixture");\n')
    signature = scene.split('std::shared_ptr<Player> Cargar(', 1)[1].split('{', 1)[0]
    head += '#define TR3_CARGAR(dir,iso) Cargar(dir,iso' + (',1' if 'int idioma' in signature else '') + ')\n'
    a = scene.index('    // Una sola pista mono:')
    b = scene.index('    return p;', a)
    # El reloj de la captura viene del manifiesto, no de un dispositivo de audio.
    scene = scene[:a] + '''    std::stable_sort(p->eventos.begin(), p->eventos.end(),
                       [](const auto& a,const auto& b){ return a.t<b.t; });
''' + scene[b:]
    (work / 'tr3_scene.inl').write_text(scene, encoding='utf-8')
    iso_reader = section(take(s, b1, 'namespace dbz3::pasarela {', 'int B1aB3('),
                         'class Iso {', 'std::string BootElf', r'''
class Iso {
 public:
  bool Open(const std::filesystem::path&) { return true; }
  std::string Find(const std::string& n) const { return Lower(n); }
  std::pair<uint32_t,uint32_t> Info(const std::string&) const { return {0,0}; }
  std::vector<uint8_t> ReadFile(const std::string& p,uint64_t off,uint32_t n) {
    const auto it=blobs.find(p+":"+std::to_string(off));
    if (it==blobs.end()) return {};
    return {it->second.begin(),it->second.begin()+std::min<size_t>(n,it->second.size())};
  }
};
''')
    hms = section(take(s, 'survivors/modelo_cpu.inl', 'SkB3 LeerSkCpu', '// Oraculo de prueba'),
                  '  std::ifstream in(', '  const bool hms3',
                  '  auto d=Fixture(ruta.filename().string());\n  if (d.size()<20 || d.size()>128u*1024*1024) return {};\n')
    cpp = work / 'tr3_cpu.cpp'
    cpp.write_text(head + iso_reader
                   + take(s, b1, 'struct AfsPs2::Impl', 'std::filesystem::path BuscarJuego(') + '}\n'
                   + take(s, ps2, 'namespace dbz3::ps2 {', 'float LoadF(') + '}\n'
                   + take(s, ps2, 'Giro LeerGiro(', 'Modelo CargarDeAfs(') + '}\n'
                   + 'namespace dbz3::pasarela::nave { struct Fig;\n'
                   + take(s, 'pasarela_nave_rueda.inl', 'struct SkB3 {', 'std::set<int> g_sk_boca_pidiendo')
                   + hms
                   + take(s, 'pasarela_nave.cpp', 'bool LeerFigura1910', 'int EstadoFiguraB3') + '}\n'
                   + (TOOLS / 'b1_escena_cpu.inc').read_text(encoding='utf-8'), encoding='utf-8')
    # Sólo consulta el build existente para flags/include paths. Se compila únicamente el arnés autónomo.
    cmd = subprocess.check_output(['ninja', '-C', 'out/build/win-amd64-dual', '-t', 'commands',
                                   f'CMakeFiles/dbz3.dir/src/{b1}.obj'], cwd=ROOT, text=True).splitlines()[-1]
    if ' -MD ' not in cmd or 'clang++' not in cmd:
        raise ValueError('Comando Clang de win-amd64-dual no reconocido')
    exe = work / 'tr3_cpu.exe'
    subprocess.run(cmd.split(' -MD ', 1)[0] + f' -I"{s}" -O1 -fuse-ld=lld "{cpp}" -o "{exe}" -lwinmm',
                   cwd=work, env=env, check=True, timeout=180)
    return exe


def render(cache, iso, out, blender=BLENDER, threads=4, samples=16):
    from PIL import Image

    cache, iso, out, blender = (p.resolve() for p in (cache, iso, out, blender))
    if out.is_relative_to(ROOT):
        raise ValueError('Las salidas con recursos deben quedar fuera del repositorio')
    spec = plan(cache)
    if not blender.is_file() or not 1 <= threads <= 64 or not 1 <= samples <= 256:
        raise ValueError('Ruta de Blender, hilos o muestras inválidos')
    if out.exists():
        raise FileExistsError(out)
    print('Comprobando huella de la ISO PAL...', flush=True)
    if iso.stat().st_size != spec['iso_size'] or sha256(iso) != spec['iso_sha256']:
        raise ValueError('La ISO PAL no coincide con la huella de la caché')
    out.mkdir()
    work = out / 'intermedios'
    work.mkdir()
    env = dict(os.environ, TEMP=str(work), TMP=str(work), BLENDER_USER_RESOURCES=str(work / 'perfil'),
               PYTHONDONTWRITEBYTECODE='1')
    write_json(out / 'cortes.json', spec)
    times = ''.join(f"{c['t_u']}\n" for c in spec['cortes'])
    exe = extractor(work, env)
    with (out / 'cpu.log').open('x', encoding='utf-8') as log, (work / 'volcado.bin').open('xb') as bundle:
        subprocess.run([exe, cache, iso], cwd=work, env=env, check=True, timeout=150,
                       input=payload(cache, iso, times), stdout=bundle, stderr=log)
    with (work / 'volcado.bin').open('rb') as bundle:
        count = 0
        while line := bundle.readline(128):
            name, length = line.decode('ascii').split()
            size = int(length)
            if Path(name).name != name or not 0 < size <= 128 << 20:
                raise ValueError('Registro de salida del arnés inválido')
            data = bundle.read(size)
            if len(data) != size:
                raise ValueError('Registro de salida del arnés truncado')
            with (work / name).open('xb') as f:
                f.write(data)
            count += 1
        if not count:
            raise ValueError('El arnés no produjo geometría')
    with (out / 'blender.log').open('x', encoding='utf-8') as log:
        subprocess.run([blender, '-b', '--factory-startup', '--disable-autoexec', '-t', str(threads),
                        '--python-exit-code', '1', '--python', TOOLS / 'b1_escena_blender.py', '--',
                        out, str(samples)], cwd=work, env=env, check=True, timeout=900,
                       stdout=log, stderr=subprocess.STDOUT)
    inventory = json.loads((out / 'geometria.json').read_bytes())
    for cut, info in zip(spec['cortes'], inventory, strict=True):
        png = out / cut['png']
        with Image.open(png) as im:
            im.load()
            if im.format != 'PNG' or list(im.size) != spec['resolucion']:
                raise ValueError(f'PNG inválido: {png}')
            sub = info['subtitulo']
            if sub >= 0:
                raw = (work / f'sub_{sub}.rgba').read_bytes()
                w, h = struct.unpack_from('<II', raw)
                sprite = Image.frombytes('RGBA', (w, h), raw[8:])
                x, y = info['sub_xy']
                frame = im.convert('RGBA')
                frame.alpha_composite(sprite, (round(x - w / 2), round(y - h / 2)))
                im.close()
                frame.convert('RGB').save(png)
        info['png_sha256'] = sha256(png)
    write_json(out / 'render_resultado.json', dict(timeline_sha256=spec['timeline_sha256'], cuadros=inventory))
    print(f"Render Blender: {len(inventory)} cortes en {out}")


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    for key in ('cache', 'iso', 'out'):
        p.add_argument(key, type=Path)
    p.add_argument('--blender', type=Path, default=BLENDER)
    p.add_argument('--hilos', type=int, default=4)
    p.add_argument('--muestras', type=int, default=16)
    args = p.parse_args()
    render(args.cache, args.iso, args.out, args.blender, args.hilos, args.muestras)
