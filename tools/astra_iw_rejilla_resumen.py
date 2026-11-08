"""Cotejo espacial por contador: descarta rescates que no son una envoltura."""
import json
import math
from pathlib import Path

OUT=Path('D:/tmp/astraIW')
CASOS=[
 ('original_kaio_x','20261101-013246_original_7D1_x',1800),
 ('original_kaio_z','20261101-020519_original_7D1_z',1800),
 ('original_kaio_esquina','20261101-025801_original_7D1_esquina',1800),
 ('port_kaio_x','20261101-025154_port_rejilla',1800),
 ('port_raditz','20261101-030149_port_rejilla',3000),
 ('port_roquedal','20261101-030610_port_rejilla',3000),
 ('port_namek','20261101-030748_port_rejilla',3000),
 ('port_cell','20261101-031223_port_rejilla',3000),
 ('port_kaio_esquina_normal','20261101-031555_port_rejilla',1800),
]

def main():
    resultados=[]
    for nombre,carpeta,periodo in CASOS:
        d=json.loads((OUT/carpeta/'medida.json').read_text(encoding='utf-8'))
        # Las lecturas port pueden repetir el reloj; conservar la ultima de cada uno.
        por_n={f['n']:f for f in d['filas']}
        filas=sorted(por_n.values(),key=lambda f:f['n'])
        cruces=[]
        for a,b in zip(filas,filas[1:]):
            saltos=[k for k in (0,2) if abs(abs(b['pos'][k]-a['pos'][k])-periodo)<5]
            if saltos:cruces.append(dict(n=b['n'],desde=a['pos'],hasta=b['pos'],ejes=saltos))
        assert cruces,(nombre,'sin envoltura real')
        n=cruces[0]['n'];velocidades=[]
        for a,b in zip(filas,filas[1:]):
            if not n-5<=a['n']<=n+12 or b['n']-a['n']!=1:continue
            delta=[b['ojo'][k]-a['ojo'][k] for k in (0,2)]
            delta=[v-math.floor((v+periodo*.5)/periodo)*periodo for v in delta]
            velocidades.append(math.hypot(*delta))
        r=dict(caso=nombre,carpeta=carpeta,periodo=periodo,cruces=cruces,
               pares_consecutivos_cerca_cruce=len(velocidades),
               max_paso_ojo_periodico=max(velocidades) if velocidades else None)
        csv=OUT/carpeta/'camara.csv'
        if csv.exists():
            vistas=[list(map(float,l.split(',')[:8])) for l in csv.read_text().splitlines() if l.strip()]
            cambios=[i for i in range(1,len(vistas)) if any(abs(vistas[i][k]-vistas[i-1][k])>periodo*.8 for k in (1,3))]
            if cambios:
                i=cambios[-1];pasos=[]
                for j in range(max(1,i-5),min(len(vistas),i+13)):
                    delta=[vistas[j][k]-vistas[j-1][k] for k in (1,3)]
                    delta=[v-math.floor((v+periodo*.5)/periodo)*periodo for v in delta]
                    pasos.append(math.hypot(*delta))
                r['traza_render']=dict(pares=len(pasos),max_paso_periodico=max(pasos),linea_cruce=i+1)
        resultados.append(r)
        print(nombre,'cruces',len(cruces),'pares',len(velocidades),'ojo_max',r['max_paso_ojo_periodico'])
        if 'traza_render' in r: print('  render',r['traza_render'])
    (OUT/'rejilla_cotejo_contadores.json').write_text(json.dumps(resultados,indent=2),encoding='utf-8')

if __name__=='__main__':main()
