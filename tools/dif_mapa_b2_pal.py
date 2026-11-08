"""Mapa de RAM y huesos de B2 PAL (SLES-51839) para captura/comparacion. Copia al repo de lo que vivia disperso en
D:/tmp/solB2X, E:/DBZ3HD_trabajo/R7/emu (sonda.py, medir_*_b2.py) y tools/solb2x_huesos_cmp3-5 (fase0C, 6/11).
Solo datos: sin imports pesados ni E/S."""
SERIAL = 'SLES-51839 (FE09A7CC)'
CONTADOR_CAMPO = 0x371550        # +1 por campo PAL (validado, pcsx2_cuadros.py)
RELOJ_PAL = 1.2                  # 1 campo PAL = 1,2 relojes de juego; el port B2 HD avanza 1,0 por cuadro
DESFASE_HD = 1.0                 # eB2h: cuadro HD c == reloj PAL c-1
HUECO_LIBRE = (0x96000, 0x100000)  # RAM libre medida para stubs/anillos (BUF_GANCHO 0xA0000 en sonda.py)
BUF_GANCHO = 0xA0000
# matrices de hueso (4x4 float, fila a fila) dentro de modelo_raw
PAL_MATS_OFFSET, PAL_MATS_PASO, PAL_N = 0x1480, 0x40, 43
HD_MATS_OFFSET, HD_MATS_PASO = 0x24b0, 0x40   # n = len(acm_nombres)
# otros contadores/direcciones B2 usados por b2t_medir/b2u_medir
ESCENA = 0x3a3e70; RUEDA = 0x3a4cb8; BURBUJA = 0x3a4cd0
# nombre de hueso -> indice de matriz (victima, actor 1) en PAL y en HD (solo los comunes valen para comparar)
PAL_IDX = {'GOK_WAIST': 1, 'GOK_STMC': 2, 'GOK_CHEST': 3, 'GOK_RCHN': 4, 'GOK_RARMROT': 5, 'GOK_RARM1': 6, 'GOK_RARM2': 7, 'GOK_RHANDROT': 8, 'GOK_L00_RHAND': 9, 'GOK_NECK': 11, 'GOK_LCHN': 22, 'GOK_LARMROT': 23, 'GOK_LARM1': 24, 'GOK_LARM2': 25, 'GOK_LLEGROT': 29, 'GOK_LLEG1': 30, 'GOK_LLEG2': 31, 'GOK_RLEGROT': 35, 'GOK_RLEG1': 36, 'GOK_RLEG2': 37}
HD_IDX = {'GOK_WAIST': 1, 'GOK_RLEGROT': 2, 'GOK_RLEG1': 3, 'GOK_RLEG2': 4, 'GOK_CHEST': 18, 'GOK_RCHN': 19, 'GOK_RARMROT': 20, 'GOK_RARM1': 21, 'GOK_RARM2': 22, 'GOK_RHANDROT': 23, 'GOK_L00_RHAND': 24, 'GOK_NECK': 26, 'GOK_LCHN': 41, 'GOK_LARMROT': 42, 'GOK_LARM1': 43, 'GOK_LARM2': 44}
