import sys,os; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
import pcsx2_sonda as s, pcsx2_pool as P
n=int(sys.argv[1]); P.registrar_en_sonda(s,n)
a=sys.argv[2:]
pn=s.Pine(espera=180)
import time
while True:
    try: pn.titulo(); break
    except Exception: time.sleep(2)
print(pn.titulo(), pn.status())
for x in a:
    if x.startswith('k:'): s.teclas(x[2:])
    elif x.startswith('c:'): print(s.captura(x[2:]))
    elif x.startswith('load:'): pn.cargar(int(x[5:])); print('loaded')
    elif x.startswith('save:'): pn.guardar(int(x[5:])); print('saved')
