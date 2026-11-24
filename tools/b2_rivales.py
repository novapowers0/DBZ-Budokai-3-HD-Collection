"""Bancos reproducibles S3-B2-RIVALES. Lee la ISO PAL sin modificarla.

Los datos extraidos son privados del jugador. La tabla distingue candidatos
estaticos de selecciones observadas; nunca inventa un banco ni un error en px.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.dont_write_bytecode = True
sys.path[:0] = [str(ROOT / 'mod center hd'), str(ROOT / 'awo_tools')]
from b1port import B2, amb_kids, Amm

ELF_PAL_SHA256 = '30c362969ad64184df0b4a4ee498badb101265470883948f3131b6302ae3c797'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def u32(data, off):
    return struct.unpack_from('<I', data, off)[0]


def guardar(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def prefijo(amo):
    import re
    m = re.search(rb'([A-Z0-9]+)_BODY(?:_1)?\x00', amo)
    return m[1].decode() if m else ''


def inventario(iso_path, out):
    out.mkdir(parents=True, exist_ok=False)
    fuente = B2(str(iso_path))
    img = fuente.iso
    elf = img.read(img.boot_elf())
    if sha(elf) != ELF_PAL_SHA256:
        raise ValueError('Solo PAL SLES_518.39 acreditado')
    afs_path = img.find('DATA_CMN.AFS')
    lba = img.files()[afs_path][0]
    def entrada(i):
        return fuente.entry(i)
    def evidencia(i):
        data = entrada(i)
        off, size = fuente.tab[2*i:2*i+2]
        return dict(entrada=i, bytes=len(data), sha256=sha(data), afs_offset=off,
                    iso_offset=lba*2048+off, tamano_tabla=size)
    bancos = []
    bank_dir = out / 'bancos_historia'
    bank_dir.mkdir()
    for i in range(800, 849):
        data = entrada(i)
        if data[1:4] != b'AMM':
            continue
        nb, names = struct.unpack_from('<II', data, 0x18)
        etiquetas = [data[names+32*k:names+32*(k+1)].split(b'\0')[0].decode('ascii') for k in range(nb)]
        cam = entrada(i-1)
        fila = dict(**evidencia(i), prefijo=etiquetas[1].split('_')[0], huesos=etiquetas,
                    camara=evidencia(i-1), camara_magic=cam[:4].decode('ascii'),
                    clips=u32(data, 0x10), seleccion_original_observada=False)
        # Copia literal: es banco original, no una animacion sintetizada.
        (bank_dir / ('%d.amm' % i)).write_bytes(data)
        (bank_dir / ('%d.amc' % (i-1))).write_bytes(cam)
        bancos.append(fila)
    personajes = []
    for cid in range(53):
        off = 0x2895C0 + cid*0x2C8
        rec = elf[off:off+0x2C8]
        cuerpo = u32(rec, 0x74)
        amo = amb_kids(entrada(cuerpo))[0][0] if cuerpo else b''
        pre = prefijo(amo)
        candidatos = [b['entrada'] for b in bancos if pre and b['prefijo'] == pre.removeprefix('X')]
        personajes.append(dict(ficha_b2=cid, nombre=rec[:32].split(b'\0')[0].decode('ascii'),
            elf_offset=off, registro_sha256=sha(rec), cuerpo=cuerpo, prefijo=pre,
            candidatos=candidatos, registro_u32=list(struct.unpack('<178I', rec))))
    filas = []
    for fase in range(1, 10):
        data = entrada(386+fase)
        n, off = struct.unpack_from('<II', data, 0x28)
        ids = sorted({data[off+40*j] for j in range(1, n) if data[off+40*j+2]})
        for cid in ids:
            p = personajes[cid]
            filas.append(dict(fase=fase, ficha_b2=cid, nombre=p['nombre'], cuerpo=p['cuerpo'],
                              prefijo=p['prefijo'], candidatos=p['candidatos']))
    mv, models = fuente.scan()
    got = {str(k): dict(anim=v[0], prefijo=v[1], registro=fuente.record(k),
                       origen=evidencia(k), anim_origen=evidencia(v[0]))
           for k, v in mv.items() if v[1] in ('GTX', 'GXL', 'GXG')}
    d = dict(iso=str(iso_path), elf=img.boot_elf(), elf_sha256=sha(elf), bancos=bancos,
             personajes=personajes, filas=filas, sin_candidato=[f for f in filas if len(f['candidatos']) != 1],
             movesets_gotenks=got, modelos_gotenks={k:v for k,v in models.items() if k in ('GTX','GXL','GXG')},
             wigs_gotenks={str(k):v for k,v in fuente.wigs().items() if v[0] in ('GTX','GXL','GXG')})
    guardar(out / 'inventario.json', d)
    fuente.f.close()
    img._f.close()
    print('Bancos originales:', len(bancos), '; filas:', len(filas), '; sin candidato:', len(d['sin_candidato']))
    print('Sin candidato:', [(f['fase'], f['ficha_b2'], f['nombre'], f['prefijo']) for f in d['sin_candidato']])
    print('Gotenks:', json.dumps(got))
    print('Modelos Gotenks:', d['modelos_gotenks'], '; pelucas:', d['wigs_gotenks'])
    return 0


def amm_info(data):
    """Cabecera y clips observados; no atribuye un uso en el tablero a un clip."""
    amm = Amm(data)
    return dict(huesos=amm.names, clips=len(amm.anims),
                clips_con_pistas=sum(a[3] is not None for a in amm.anims),
                cuadros=[a[2] for a in amm.anims])


def preparar(iso_path, inventario_path, out):
    inv_bytes = inventario_path.read_bytes()
    inv = json.loads(inv_bytes)
    fuente = B2(str(iso_path))
    try:
        elf = fuente.iso.read(fuente.iso.boot_elf())
        if sha(elf) != ELF_PAL_SHA256 or inv['elf_sha256'] != sha(elf):
            raise ValueError('El ELF o el inventario no corresponden a B2 PAL acreditado')
        # Una carpeta nueva conserva cualquier entrega anterior.
        out.mkdir(parents=True, exist_ok=False)
        artefactos = []

        def escribir(rel, data, origen):
            path = out / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            with path.open('xb') as f:
                f.write(data)
            artefactos.append(dict(ruta=rel, bytes=len(data), sha256=sha(data), origen=origen))
            return rel

        def copiar_entrada(i, carpeta):
            data = fuente.entry(i)
            off, size = fuente.tab[2*i:2*i+2]
            return escribir('%s/%d.amb' % (carpeta, i), data,
                            dict(tipo='entrada', entrada=i, afs_offset=off, bytes_tabla=size))

        pendientes = inv['sin_candidato']
        prefijos = {p['prefijo'].removeprefix('X') for p in pendientes if p['prefijo']}
        prefijos.update(('GTX', 'GXL', 'GXG'))
        catalogo, seleccionados, errores, pares = [], {}, [], []
        # Incluye AMM dentro de AMB: el barrido viejo miraba solo bancos sueltos 801..848.
        for i in range(len(fuente.tab)//2):
            data = fuente.entry(i)
            partes = [(None, data)]
            if data[:4] == b'#AMB':
                partes = list(enumerate(x[0] for x in amb_kids(data)))
            for hijo, amm in partes:
                if amm[1:4] != b'AMM':
                    continue
                try:
                    nb, no = struct.unpack_from('<II', amm, 0x18)
                    if no + nb*32 > len(amm):
                        raise ValueError('tabla de nombres fuera del AMM')
                    nombres = [amm[no+32*k:no+32*(k+1)].split(b'\0')[0].decode('latin1') for k in range(nb)]
                    pres = sorted({n.split('_')[0].removeprefix('X') for n in nombres if '_' in n})
                    fila = dict(entrada=i, hijo=hijo, sha256=sha(amm), bytes=len(amm),
                                prefijos=pres, huesos=len(nombres), clips=u32(amm, 0x10))
                    # Un AMM de combate no sustituye a uno del panel. Conservamos
                    # aparte los pares sueltos AMC+AMM, sin elegir su clip.
                    if hijo is None and i > 0 and len(nombres) >= 2:
                        camara = fuente.entry(i-1)
                        if camara[:4] == b'#AMC':
                            off, size = fuente.tab[2*(i-1):2*i]
                            pares.append(dict(entrada=i, camara=i-1,
                                              prefijo_segundo_hueso=nombres[1].split('_')[0],
                                              clips=u32(amm, 0x10),
                                              camara_clips=u32(camara, 0x10),
                                              camara_sha256=sha(camara),
                                              camara_bytes=len(camara)))
                            if prefijos.intersection(pres):
                                escribir('camaras_candidatas/%d.amc' % (i-1), camara,
                                         dict(tipo='entrada', entrada=i-1, afs_offset=off,
                                              bytes_tabla=size))
                    if prefijos.intersection(pres):
                        fila['detalle'] = amm_info(amm)
                        rel = 'amm_candidatos/%d_%s.amm' % (i, 'directo' if hijo is None else 'h%d' % hijo)
                        fila['archivo'] = escribir(rel, amm, dict(tipo='amm', entrada=i, hijo=hijo))
                        for pre in prefijos.intersection(pres):
                            seleccionados.setdefault(pre, []).append(fila)
                    catalogo.append(fila)
                except (struct.error, ValueError, IndexError) as exc:
                    errores.append(dict(entrada=i, hijo=hijo, error=str(exc)))

        tabla = []
        for fila in inv['filas']:
            pre = fila['prefijo'].removeprefix('X')
            tabla.append(dict(**fila, bancos_amm_corpus=seleccionados.get(pre, []),
                              pares_amm_amc_corpus=[p for p in pares if pre and
                                                   p['prefijo_segundo_hueso'] == pre],
                              seleccion_original_observada=False, clip_original=None,
                              error_px=None, estado='PENDIENTE_REFERENCIA_Y_MEDICION'))

        mv, modelos = fuente.scan()
        recursos = {134, 201, 203}
        for pre in ('GTX', 'GXL', 'GXG'):
            recursos.update(modelos.get(pre, []))
        rutas = {i: copiar_entrada(i, 'gotenks/recursos') for i in sorted(recursos)}
        registros = []
        for p in inv['personajes']:
            if not p['nombre'].startswith('GOTENKS'):
                continue
            off = p['elf_offset']
            raw = elf[off:off+0x2C8]
            rel = escribir('gotenks/registro_%d.bin' % p['ficha_b2'], raw,
                           dict(tipo='elf', offset=off, bytes=0x2C8))
            registros.append(dict(ficha_b2=p['ficha_b2'], nombre=p['nombre'], archivo=rel,
                                  cuerpo_campo_74=p['cuerpo'], prefijo_cuerpo=p['prefijo'],
                                  campo_30_u16=struct.unpack_from('<H', raw, 0x30)[0],
                                  palabras_14c_18c=list(struct.unpack_from('<16I', raw, 0x14C))))
        gotenks = dict(estado='RECURSOS_ORIGINALES_PREPARADOS_NO_FICHA_RUNTIME_ACREDITADA',
                       registros=registros, moveset=201, animaciones=203,
                       registro_moveset=fuente.record(201), modelos={p:modelos.get(p, []) for p in ('GTX','GXL','GXG')},
                       recursos=rutas, aura=None, habilidades_seleccion=None, formas_runtime=None,
                       advertencias=[
                           'La ficha ELF 36 usa un cuerpo con prefijo GTN; no acredita el cuerpo GTX.',
                           'El importador B2 conserva tecnicas, definitiva y voz del donante.',
                           'ficha_nativa=64 utiliza datos HD, no demuestra una ficha propia de B2.',
                           'No retirar guarda_donante_b2 sin registro char372 y prueba de reset compatibles.'])
        resultado = dict(elf_sha256=sha(elf), inventario_sha256=sha(inv_bytes), iso=str(iso_path),
                          artefactos=artefactos, catalogo_amm=catalogo, pares_amm_amc=pares,
                          errores_catalogo=errores,
                         tabla_rivales=tabla, gotenks=gotenks,
                          cobertura=dict(filas=len(tabla), sin_banco_historia=len(pendientes),
                                         sin_par_candidato_corpus=sum(
                                             len(f['pares_amm_amc_corpus']) != 1 for f in tabla),
                                        filas_medidas_px=0, parejas_angulares_reales=0,
                                        fusiones_reales_nuevas=0, qte_real_nuevo=False))
        guardar(out / 'datos.json', resultado)
        print('Datos preparados: %d archivos; %d AMM en todo DATA_CMN; %d errores de analisis' % (
            len(artefactos), len(catalogo), len(errores)))
        print('Rivales pendientes:', [(p['ficha_b2'], p['prefijo'], len(seleccionados.get(p['prefijo'].removeprefix('X'), [])))
                                      for p in pendientes])
        print('Pares AMC/AMM fuera del rango antiguo:', [
            (f['fase'], f['ficha_b2'], [(p['camara'], p['entrada']) for p in f['pares_amm_amc_corpus']])
            for f in tabla if not f['candidatos'] and f['pares_amm_amc_corpus']])
        print('Gotenks:', json.dumps(gotenks, ensure_ascii=False))
        return 1 if errores else 0
    finally:
        fuente.f.close()
        fuente.iso._f.close()


def verificar(iso_path, carpeta, informe=None):
    """Coteja cada copia con la ISO de nuevo, no solo contra un hash autodeclarado."""
    datos = json.loads((carpeta / 'datos.json').read_text(encoding='utf-8'))
    fuente = B2(str(iso_path))
    try:
        elf = fuente.iso.read(fuente.iso.boot_elf())
        if sha(elf) != ELF_PAL_SHA256 or datos['elf_sha256'] != sha(elf):
            raise ValueError('ELF PAL diferente')
        comprobaciones = []
        for item in datos['artefactos']:
            origen = item['origen']
            if origen['tipo'] == 'elf':
                original = elf[origen['offset']:origen['offset']+origen['bytes']]
            else:
                original = fuente.entry(origen['entrada'])
                if origen['tipo'] == 'amm' and origen['hijo'] is not None:
                    original = amb_kids(original)[origen['hijo']][0]
            path = (carpeta / item['ruta']).resolve()
            if not path.is_relative_to(carpeta.resolve()):
                raise ValueError('Ruta fuera de la entrega')
            copia = path.read_bytes()
            ok = copia == original and sha(copia) == item['sha256'] and len(copia) == item['bytes']
            comprobaciones.append(dict(archivo=item['ruta'], ok=ok))
        sin_banco = [f for f in datos['tabla_rivales'] if len(f['candidatos']) != 1]
        formato_ok = (len(datos['tabla_rivales']) == 58 and len(sin_banco) == 13
                      and all(f['error_px'] is None and not f['seleccion_original_observada'] for f in datos['tabla_rivales'])
                      and datos['gotenks']['registro_moveset']['prefijo'] == 'GTX'
                      and not datos['errores_catalogo'])
        ok = formato_ok and all(c['ok'] for c in comprobaciones) and bool(comprobaciones)
        res = dict(ok=ok, formato_ok=formato_ok, archivos=comprobaciones,
                   datos_sha256=sha((carpeta / 'datos.json').read_bytes()),
                   elf_sha256=sha(elf),
                   solo_integridad_estatica=True, juego_validado=False)
        if informe is not None:
            with informe.open('x', encoding='utf-8') as f:
                f.write(json.dumps(res, ensure_ascii=False, indent=2) + '\n')
        # Informe a stdout para poder repetir sin reemplazar evidencia anterior.
        print(json.dumps(res, ensure_ascii=False, indent=2))
        return 0 if ok else 1
    finally:
        fuente.f.close()
        fuente.iso._f.close()


def fusiones(iso_path, banco, informe):
    """Reejecuta el banco existente, sin compilar ni escribir en la entrega ajena.

    Contrasta su salida completa con el sorteo PAL. El ejecutable del banco es
    evidencia separada del dbz3.exe y no acredita fusiones ni QTE en combate.
    """
    fuente = B2(str(iso_path))
    try:
        elf = fuente.iso.read(fuente.iso.boot_elf())
    finally:
        fuente.f.close()
        fuente.iso._f.close()
    if sha(elf) != ELF_PAL_SHA256:
        raise ValueError('Solo PAL SLES_518.39 acreditado')

    def archivo_va(va, size):
        phoff, = struct.unpack_from('<I', elf, 0x1C)
        phnum, = struct.unpack_from('<H', elf, 0x2C)
        for i in range(phnum):
            tipo, off, base, _, n = struct.unpack_from('<IIIII', elf, phoff + 32*i)
            if tipo == 1 and base <= va and va + size <= base + n:
                return off + va - base
        raise ValueError('VA fuera de PT_LOAD: %X' % va)

    pesos = list(struct.unpack_from('<6b', elf, archivo_va(0x393B38, 6)))
    excl_off = archivo_va(0x393B40, 18)
    excl = [list(struct.unpack_from('<3b', elf, excl_off + 3*k)) for k in range(6)]
    src = ROOT / 'src/pasarela_b2.cpp'
    texto = src.read_text(encoding='utf-8')
    tabla = re.search(r'kB2aB3\[kB2Personajes\] = \{(.*?)\};', texto, re.S).group(1)
    mapa = [int(x) for x in re.findall(r'-?\d+', re.sub(r'//[^\n]*', '', tabla))]
    if len(mapa) != 53 or any(p <= 0 for p in pesos):
        raise ValueError('Tabla de personajes o pesos inesperados')
    rivales = [100 if i == 37 else c for i, c in enumerate(mapa)]
    salida = subprocess.run([str(banco.resolve()), *map(str, rivales), '1000'],
                            capture_output=True, text=True, check=True, timeout=30).stdout
    sorteos, mascaras = {}, {}
    for linea in salida.splitlines():
        campos = linea.split()
        if campos[0] == 'S' and len(campos) == 4:
            key = tuple(map(int, campos[1:3]))
            if key in sorteos:
                raise ValueError('Sorteo duplicado en el banco')
            sorteos[key] = int(campos[3])
        elif campos[0] == 'M' and len(campos) == 3:
            rival, valor = map(int, campos[1:])
            if rival in mascaras and mascaras[rival] != valor:
                raise ValueError('Mascara contradictoria')
            mascaras[rival] = valor
        else:
            raise ValueError('Salida de banco desconocida')
    esperados = {(m, r) for m in range(63)
                 for r in range(sum(p for k, p in enumerate(pesos) if not (m >> k & 1)))}
    diferencias = []
    for m, r in sorted(esperados):
        resto, k = r, -1
        while True:
            k = (k + 1) % 6
            if m >> k & 1:
                continue
            resto -= pesos[k]
            if resto <= 0:
                break
        if sorteos.get((m, r)) != k:
            diferencias.append(dict(mascara=m, r=r, original=k, banco=sorteos.get((m, r))))
    dif_m = []
    for cid, rival in enumerate(rivales):
        original = sum(1 << k for k, ids in enumerate(excl) if cid in ids)
        if mascaras.get(rival) != original:
            off_nombre = 0x2895C0 + cid*0x2C8
            nombre = elf[off_nombre:off_nombre+32].split(b'\0')[0].decode('ascii')
            dif_m.append(dict(ficha_b2=cid, nombre=nombre, rival_b3=rival, original=original,
                              banco=mascaras.get(rival)))
    header = ROOT / 'src/fusion_b2.h'
    snapshot = banco.parent / 'src/fusion_b2.h'
    mismo_header = snapshot.is_file() and snapshot.read_bytes() == header.read_bytes()
    mismo_texto = (snapshot.is_file() and
                   snapshot.read_text(encoding='utf-8') == header.read_text(encoding='utf-8'))
    dominio_ok = set(sorteos) == esperados
    res = dict(elf_sha256=sha(elf), banco=str(banco), banco_sha256=sha(banco.read_bytes()),
               header_actual_sha256=sha(header.read_bytes()),
               header_snapshot_identico=mismo_header,
               header_snapshot_texto_identico=mismo_texto, pesos=pesos, exclusiones=excl,
               sorteos_comparados=len(esperados), dominio_completo=dominio_ok,
               diferencias_sorteo=diferencias, rivales_comparados=53,
               diferencias_exclusion=dif_m, potara_excluye_vegeta=bool(mascaras.get(1000, 0) & 4),
               solo_banco_independiente=True, juego_validado=False, qte_validado=False)
    res['sorteo_ok'] = dominio_ok and mismo_texto and not diferencias
    res['ok'] = (res['sorteo_ok'] and not dif_m
                 and res['potara_excluye_vegeta'])
    with informe.open('x', encoding='utf-8') as f:
        f.write(json.dumps(res, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps(res, ensure_ascii=False, indent=2))
    return 0 if res['ok'] else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    sp = ap.add_subparsers(dest='cmd', required=True)
    i = sp.add_parser('inventario')
    i.add_argument('--iso', type=Path, required=True)
    i.add_argument('--salida', type=Path, required=True)
    p = sp.add_parser('preparar')
    p.add_argument('--iso', type=Path, required=True)
    p.add_argument('--inventario', type=Path, required=True)
    p.add_argument('--salida', type=Path, required=True)
    v = sp.add_parser('verificar')
    v.add_argument('--iso', type=Path, required=True)
    v.add_argument('--carpeta', type=Path, required=True)
    v.add_argument('--informe', type=Path, help='Informe nuevo, sin sobrescribir otro')
    f = sp.add_parser('fusiones')
    f.add_argument('--iso', type=Path, required=True)
    f.add_argument('--banco', type=Path, required=True, help='Ejecutable del banco existente; no compila')
    f.add_argument('--informe', type=Path, required=True)
    a = ap.parse_args()
    if a.cmd == 'fusiones':
        return fusiones(a.iso, a.banco, a.informe)
    if a.cmd == 'preparar':
        return preparar(a.iso, a.inventario, a.salida)
    if a.cmd == 'verificar':
        return verificar(a.iso, a.carpeta, a.informe)
    return inventario(a.iso, a.salida)


if __name__ == '__main__':
    raise SystemExit(main())
