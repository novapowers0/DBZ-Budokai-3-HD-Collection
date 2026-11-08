r"""Navegacion de PCSX2 (plaza 2, B2 PAL) hasta un combate de PRACTICA (el COM no ataca) con dos personajes, y guarda un savestate en --slot.
Tal como se uso el 7/11 (N3b) para crear los estados de b2_huesos.py capturar. Requiere la plaza 2 lanzada con `pcsx2_pool.py lanzar 2 ... --estado "SLES-51839 (FE09A7CC).20.p2s"`
(menu principal; la ISO con ruta real D:\DBZ3HD\ps2_games\..., no la relativa).
uso: python tools/b2_nav_practice.py <slot> <vueltas_1P> <vueltas_COM> <estadio_derecha>
  vueltas: positivo = derecha, negativo = izquierda en la rueda de 30 (posicion: 0 Yamcha,1 Tien,2 Hercule,3 Supreme Kai,4 Dabura,5 Majin Buu,6 Super Buu,7 Kid Buu,8 A20,9 Cell,10 A18,11 A17,12 A16,13 Frieza,
  14 Ginyu,15 Recoome,16 Raditz,17 Nappa,18 Vegeta,19 Trunks,20 Kid Trunks,21 Goten,22 Random,23 Goku,24 Teen Gohan,25 Gohan,26 Saiyaman,27 Videl,28 Piccolo,29 Krillin); ambos empiezan en Goku (23).
  estadio_derecha = pulsaciones a la derecha en la seleccion de estadio: 0 -> estadio 0 (g4), 2 -> 5 (g7), 4 -> 1 (g8), 5 -> 4 (g0), 6 -> 6 (g2), 3 -> 7, 7 -> 2.
  Ejemplos: Cell vs Piccolo g4 = `150 -14 5 0`; Freezer vs Vegeta g7 = `154 -10 -5 2`. Despues: python tools/b2_huesos.py capturar --slot <slot> --grupo <g>.
  Tras b2_huesos capturar el PCSX2 queda pausado; reanudar() lo arregla (F6)."""
import os,sys,time,struct
import zipfile,io
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
os.environ['DBZ3_AGENTE']='dia1B'
import pcsx2_sonda as s, pcsx2_pool as PL
PL.registrar_en_sonda(s,2)
from PIL import Image
p=s.Pine(espera=30)
def shot(nombre):
    est='D:/DBZ3HD/_pcsx2_pool/2/sstates/SLES-51839 (FE09A7CC).99.p2s'
    if os.path.exists(est): os.replace(est,est+'.old')
    p.guardar(99)
    for _ in range(100):
        time.sleep(.2)
        try:
            with zipfile.ZipFile(est) as z: png=z.read('Screenshot.png')
            im=Image.open(io.BytesIO(png)).convert('RGB'); im=im.resize((im.width//2,im.height//2)); im.save('E:/DBZ3HD_trabajo/dorados_noche/b2/huesos/nav_%s.png'%nombre); return
        except Exception as e: err=e

def reanudar():
    import pcsx2_cuadros as C
    c=object.__new__(C.Cuadros);c.s,c.p,c.contador=s,p,0x371550
    if p.status()==1:
        c.pulso(0x75);time.sleep(1)
    return p.status()

if __name__=='__main__':
    """nav_par.py <slot> <vueltas_1P(+derecha/-izq)> <vueltas_COM> <stage_derecha>  -> deja un estado de Practice en el slot"""
    slot,n1,n2,st=map(int,sys.argv[1:5]); STG=int(sys.argv[5]) if len(sys.argv)>5 else st
    d=lambda n:('right*%d'%n) if n>0 else ('left*%d'%-n) if n<0 else 'espera10'
    reanudar()
    p.cargar(20);time.sleep(4)
    s.teclas('down*3 espera300 cruz espera5500 cruz espera4500')
    s.teclas(d(n1)+' espera300 cruz espera1200 cruz espera1200 '+d(n2)+' espera300 cruz espera1200 cruz espera2500')
    if st>0: s.teclas('right*%d espera500'%STG)
    shot('%d_sel'%slot)
    if slot==0: sys.exit()
    s.teclas('cruz espera6000')
    u16=lambda a:struct.unpack('<H',p.leer(a,2))[0]
    for i in range(120):
        mgr=p.u32(0x3a44b4)
        if mgr>0x100000:
            f=[p.u32(mgr+0x210+0x9f50*k+0x754) for k in range(2)]
            if f[0] and f[1] and u16(mgr+0x14304) in (2,3): break
        time.sleep(1)
    print('stage',p.u32(0x373460),[hex(x) for x in f])
    time.sleep(4)
    p.guardar(slot);time.sleep(3);shot('%d'%slot)
    