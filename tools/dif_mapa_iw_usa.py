"""dif_mapa_iw_usa.py - direcciones de RAM de Infinite World USA (SLUS-21842) frente a EU (SLES-55347), medido el 6/11 (corregido el 6/11 tarde).

Metodo: se enmascaran los inmediatos de lui/lw/sw/addiu y se buscan en el ELF USA las mismas secuencias que en el EU (9339 anclas unicas, ELF de
D:/.../ps2_games y del banco EU). Resultado:
  - datos y bss (>= 0x4A0000): USA = EU - 0x800 (cientos de anclas, constante). Comprobado en juego: el contador 0x4A4D40 (EU) esta en 0x4A4540 y avanza,
    la clave de mision 0x5C0DC8 (EU) esta en 0x5C05C8 y vale 0x7D0 al empezar la primera mision.
  - 0x49xxxx (contexto de IW, gp-relativo): el desplazamiento depende del tramo. Tabla RANGOS_DATOS abajo, con las parejas medidas en
    src/iw_mips.cpp MapaUsa() (ver PARES_MEDIDOS). Fuera de los tramos: -0x800 por suposicion.
  - codigo (0x100000-0x3A0000): el delta cambia por tramos (0, -16, -32 ... -1280, +320...): tabla en dif_mapa_iw_usa_codigo.json (100 tramos),
    funcion pc_eu_a_usa(). Los ganchos REX_HOOK_RAW/lector de funciones del EU hay que pasarlos por ella (o rehacer el emparejado).
Uso: from dif_mapa_iw_usa import *; usa(0x4A4D40) -> 0x4A4540; usa_datos(0x49CBC0) -> 0x49C3B0.
"""
import bisect, json, os

DATOS = -0x800                   # por defecto: datos fuera de los tramos medidos (sin medir: 0x49CA80-0x49CAFF, 0x49CC00-0x49CCFF, 0x49CF00+)

# Tramos medidos (EU inicio, EU fin excluido, delta USA-EU), ordenados por inicio.
RANGOS_DATOS = [
    (0x49BE00, 0x49CA80, -0x800),   # semilla, paso 0x49C7DC, pantalla 0x49C84C/850, gestor 0x49CA70
    (0x49CB00, 0x49CC00, -0x810),   # 0x49CBxx: contexto de marcha 0x49CBC0 -> 0x49C3B0
    (0x49CD00, 0x49CE00, -0x820),   # 0x49CDxx (kPCtx19, kPCam, kPUia, kPGrid, kPVctx medidos)
    (0x49CE00, 0x49CF00, -0x81C),   # 0x49CExx: kPView 0x49CE78 -> 0x49C65C (src/iw_mips.cpp MapaUsa, medido)
]
_INI = [a for a, _, _ in RANGOS_DATOS]

# Parejas (EU, USA) medidas en src/iw_mips.cpp MapaUsa(). El lado EU de cada una viene de los tools del repo que leen esa misma
# posicion (p. ej. contexto 0x49CBC0, pantalla 0x49C84C, paso 0x49C7DC, visor 0x49CDF0); las demas son del informe §11.
PARES_MEDIDOS = [
    (0x4A3DF0, 0x4A35F0, 'gp (crt0 USA)'),           # informe §11: gp EU 0x4A3DF0 -> 0x4A35F0
    (0x49C7DC, 0x49BFDC, 'paso kPasoT'),              # -0x800
    (0x49C84C, 0x49C04C, 'pantalla ancho kPAncho'),   # -0x800
    (0x49C850, 0x49C050, 'pantalla alto kPAlto'),     # -0x800
    (0x49CBB8, 0x49C3A8, 'kPMgw'),                    # -0x810
    (0x49CBC0, 0x49C3B0, 'contexto marcha kPObjs'),   # -0x810
    (0x49CD68, 0x49C548, 'kPCtx19 (trabajo)'),        # -0x820
    (0x49CD6C, 0x49C54C, 'kPCam (camara)'),           # -0x820
    (0x49CD8C, 0x49C56C, 'kPUia (ui)'),               # -0x820
    (0x49CDB8, 0x49C598, 'kPGrid (stage)'),           # -0x820
    (0x49CDF0, 0x49C5D0, 'kPVctx (visor)'),           # -0x820
    (0x49CE78, 0x49C65C, 'kPView'),                   # -0x81C
]

COUNT = 0x4A4540                 # EU 0x4A4D40
CLAVE_ADDR = 0x5C05C4            # EU 0x5C0DC4 (indice, 4 bytes) ; la clave esta en +4 = 0x5C05C8
CLAVE = 0x5C05C8                 # EU 0x5C0DC8
PAD_IDX = 0x5C0CB4               # EU 0x5C14B4 (indice de mando original)
PAD_RAW = 0x4AD6DA               # EU 0x4ADEDA (+pad*0xBC)

_COD = None


def usa(addr_eu):
    """Direccion de DATOS EU >= 0x4A0000 -> USA (verificado: -0x800)."""
    if addr_eu < 0x4A0000:
        raise ValueError('fuera del tramo verificado; usar usa_datos (0x49xxxx) o pc_eu_a_usa (codigo)')
    return addr_eu + DATOS


def usa_datos(addr_eu):
    """Direccion de DATOS EU 0x49xxxx (o mayor) -> USA: delta del tramo medido; fuera de la tabla, DATOS (-0x800, sin medir)."""
    i = bisect.bisect_right(_INI, addr_eu) - 1
    if i >= 0 and addr_eu < RANGOS_DATOS[i][1]:
        return addr_eu + RANGOS_DATOS[i][2]
    return addr_eu + DATOS


WORK = usa_datos(0x49CBC0)       # 0x49C3B0 (medido)
ROOTS = tuple(usa_datos(a) for a in (0x49CA70, 0x49CBBC, 0x49CD8C, 0x49CBA4, 0x49CE3C))   # raices del EU; 0x49CA70 -> 0x49C270 (el informe §11 dice 0x49C280: revisar)
QTE_CTX = {k: usa_datos(v) for k, v in {'NB014': 0x49CDF8, 'NB013': 0x49CE7C, 'NB006': 0x49CEBC}.items()}  # NB014/NB013/NB006 USA derivados del tramo, sin medir


def pc_eu_a_usa(pc):
    """Direccion de CODIGO EU (0x100000-0x3A0000) -> USA por el tramo de anclas mas cercano por debajo (error posible +-16 bytes en bordes de tramo)."""
    global _COD
    if _COD is None:
        _COD = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'dif_mapa_iw_usa_codigo.json')))
    i = bisect.bisect_right([a for a, _ in _COD], pc) - 1
    return pc + _COD[max(i, 0)][1]


if __name__ == '__main__':
    assert usa(0x4A4D40) == COUNT and usa(0x5C0DC8) == CLAVE and usa(0x5C0DC4) == CLAVE_ADDR
    assert usa(0x5C14B4) == PAD_IDX and usa(0x4ADEDA) == PAD_RAW
    for eu, us, nombre in PARES_MEDIDOS:
        assert usa_datos(eu) == us, (nombre, hex(eu), hex(usa_datos(eu)), hex(us))
    assert WORK == 0x49C3B0
    # fuera de los tramos medidos: -0x800 por defecto
    assert usa_datos(0x49CA90) == 0x49C290 and usa_datos(0x49CC80) == 0x49CC80 - 0x800 and usa_datos(0x49CF10) == 0x49CF10 - 0x800
    print('mapa IW USA OK', hex(pc_eu_a_usa(0x200000)), len(PARES_MEDIDOS), 'parejas medidas')
