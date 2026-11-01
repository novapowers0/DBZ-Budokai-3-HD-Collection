#!/usr/bin/env python3
"""Ejecuta la etapa xv2 de heroe_e2e con una sola copia fija y respaldo restaurable.

Las capturas se omiten en N1 oculto; no se presentan como prueba visual.
"""
import argparse
from datetime import datetime
import json
import os
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT/'tools'), str(Path(__file__).parent)]


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--copia', choices=('smoke_a','smoke_b','smoke_c','smoke_d','smoke_e','smoke_f','sol','sol2'), default='smoke_c')
    ap.add_argument('--trabajo', type=Path, default=Path('D:/tmp/xv4'))
    ap.add_argument('--visible', action='store_true')
    a = ap.parse_args()
    import pruebas as P
    import heroe_e2e as H
    if not a.trabajo.is_dir():
        ap.error('crea primero --trabajo')
    work = a.trabajo/('heroe_'+datetime.now().strftime('%Y%m%d_%H%M%S_%f'))
    work.mkdir()
    backup = Path('D:/_PARA_BORRAR_CLAUDE/xv')/work.name
    backup.mkdir(parents=True)
    dest = P.BASE/a.copia
    P.comprobar_libre(dest)
    lock = P.BASE/('_en_uso_'+a.copia)/'pid'
    if lock.is_file():
        owner = lock.read_text().split()
        if owner and P._vivo(int(owner[0]), owner[1] if len(owner)>1 else ''):
            ap.error('la copia esta reservada por otra prueba; elige otra copia fija')
    P.PRUEBAS, P.PAPELERA = work, backup/'respaldos_pruebas'
    P.VENTANAS = 1
    P.OVERRIDES.update(dbz3_region='"us"', dbz3_cac_xv2='true')
    os.environ.update(TEMP=str(a.trabajo), TMP=str(a.trabajo), DBZ3_CAC_CACHE=str(a.trabajo/'cache'),
                      DBZ3_CAC_XV2='1', DBZ3_TEST_OCULTO='0' if a.visible else '1')
    class Capture(P.Juego):
        def captura(self, name):
            if not a.visible: return None
            p = super().captura(name)
            from PIL import Image
            full = self.run/'full'; full.mkdir(exist_ok=True)
            saved = full/p.name; p.rename(saved)
            with Image.open(saved) as im:
                im.resize((im.width//2, im.height//2), Image.Resampling.LANCZOS).save(p)
            return p
    P.Juego = Capture
    results = []
    def correr(copia, scenario):
        result = P._correr(copia, scenario, forzar=True)
        log = (Path(result['run'])/'dbz3.log').read_text(encoding='utf-8',errors='replace')
        results.append(result)
        # El verificador historico espera las etiquetas inglesas; el juego
        # tambien las emite en castellano. Solo normalizamos estas dos etiquetas.
        normalized = log.replace('XV2: Pelo =', 'XV2: Hair =').replace('XV2: Cara =', 'XV2: Face =')
        return result['ok'], result.get('msg',''), result['run'], normalized
    H.correr_escenario = correr
    def reiniciar(copia):
        dst = backup/('progreso_'+datetime.now().strftime('%H%M%S_%f')); dst.mkdir()
        for name in ('rpg.toml','hub_heroe.toml','cac.toml','cac.toml.bak','cac.toml.v1.bak','rpg.toml.bak'):
            p=P.BASE/copia/'user_data/dbz3'/name
            if p.exists(): shutil.move(str(p),str(dst/name))
    H.reiniciar_progreso = reiniciar
    ud = dest/'user_data/dbz3'
    state = backup/'original'; state.mkdir()
    names = ('rpg.toml','hub_heroe.toml','cac.toml','cac.toml.bak','cac.toml.v1.bak','rpg.toml.bak')
    changed=[]
    with P.copia_en_uso(dest):
        for name in names:
            p=ud/name
            if p.exists(): shutil.move(str(p),str(state/name))
        for name in ('dbz3.exe','rexruntime.dll','rexgpu-xenos.dll','dbz3_user.toml'):
            if (dest/name).is_file(): shutil.copy2(dest/name, backup/name)
        for m in (dest/'mods').iterdir():
            if m.is_dir() and not (m/'.disabled').exists():
                (m/'.disabled').write_text('xv4 prueba temporal',encoding='utf-8'); changed.append(m)
        (backup/'estado.json').write_text(json.dumps(dict(copy=str(dest), active_mods=[str(m) for m in changed], names=names)),encoding='utf-8')
        try:
            H.etapa_juego(a.copia, 'xv2')
        finally:
            reiniciar(a.copia)
            for name in names:
                if (state/name).exists(): shutil.move(str(state/name),str(ud/name))
            for m in (dest/'mods').iterdir():
                if m.is_dir() and (m.name.startswith('heroe_') or m.name.startswith('xv2_')) and not (m/'.disabled').exists():
                    (m/'.disabled').write_text('prueba XV4 terminada',encoding='utf-8')
            for i,m in enumerate(changed):
                if (m/'.disabled').exists(): shutil.move(str(m/'.disabled'),str(backup/('marker_%d.disabled'%i)))
    result=dict(checks=H.RES,runs=results,backup=str(backup),passed=bool(H.RES) and all(r[1] for r in H.RES))
    (work/'resultado.json').write_text(json.dumps(result,indent=2,ensure_ascii=False,default=str),encoding='utf-8')
    print('heroe_e2e xv2: %d pruebas, %d fallos; %s' % (len(H.RES),sum(not r[1] for r in H.RES),work))
    return 0 if result['passed'] else 1


if __name__=='__main__':
    sys.exit(main())
