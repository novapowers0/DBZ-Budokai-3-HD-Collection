# Uso de los recursos (assets) — estado 2026-10-17

Regla: nada del juego (B3, B1, B2, IW, T3) se redistribuye. Lo del juego se lee **en tiempo de ejecución de la copia
del jugador**; lo que va en el release es arte propio (del autor o generado por código) o del autor con su permiso.

## Qué se usa y de dónde sale
| Recurso | Origen | Dónde se usa | Cómo llega |
|---|---|---|---|
| Marcos, paneles, caras, cintas del menú de B3 | AFS del jugador (data_usi/data_cmn) | HUB, Tag Team, Online, modos | `native_ui.cpp` los extrae al arrancar (también desde la ISO: copia `iso_datos`) |
| Fondos por pantalla (HUB, Héroe, Lab, Ranking, Misión 100, retos) | Pack Dragon HUB del autor (`assets_propios/png/add`, `dragon_hub/`) | `modos.cpp` (fondo + anfitrión por pantalla), `hub_panel.cpp` | `generar.py --al-juego` → `res/ingame_ui/hub/` |
| Fondo de Online (Torneo del Otro Mundo) y `hub/fondo` (torneo, Tournament Background; Torre, Supervivencia, Kumite, Pasarela) | Pack Dragon HUB | `online_panel.cpp`, `modos.cpp`, `pasarela_misiones.cpp` | idem |
| Anfitriones (Goku, Vegeta, Whis, Trunks, héroe CC; Paikuhan en Online, Gohan del Futuro en el menú de Pasarela) | Pack Dragon HUB | pantallas de modos | idem (recortados a 437 px como el Kaio del Duelo). En el release van Trunks, Paikuhan y Gohan; `make_release.ps1` omite Goku, Vegeta, Bulma, Whis y héroe |
| Placas de rango KYU 5 … MAESTRO | Pack Dragon HUB | HUB (placa + ascenso) | `hub/rango_NN(.png/_vacia)` |
| Vuelos a cámara (Trunks, C17), icono de guardado SSJ4 | Pack Dragon HUB | entrada al HUB, aviso de guardado | `hub/vuelo_N`, `hub/guardado*` |
| Etiquetas, insignias, BETA, logo | Generadas por código (CC0) + logo del autor | HUB, launcher (cabecera) | `assets_propios/generar.py` |
| Campañas, mapas, minijuegos, poses, vídeos de IW | ISO/carpeta de IW del jugador | Pasarela IW, minijuegos | `pasarela_b1.cpp` BuscarIW / `.ruta` (botón "Elegir ISO") |
| Tablero y Nave de B2 | ISO de B2 del jugador | Pasarela B2 | BuscarJuego("budokai 2") / `.ruta` |
| Leyenda de Satán (charlas, tabla de combates) | ISO de B1 del jugador | Leyenda de Mr. Satán (modelos de B3, Cell Ring = mapa 50 de B3) | BuscarIsoB1 / `.ruta` |
| Personajes portados (B1/B2/IW) | ISO del jugador + Mod Kit | Personajes nuevos | `roster_build.py` / `importar.py` |
| Escenarios de PS2 (campos de IW, B1/B2) | ISO del jugador | Escenarios nuevos | `campos_iw.py` al pulsar JUGAR |
| Arte de terceros (renders, wallpapers) | Carpeta del usuario | **Solo** packs opcionales locales | `mod center hd/packs_visuales/` (nunca en el release) |

## Lo que queda por aprovechar (con dato medido)
1. **Tenkaichi 3 del jugador** (estudio 2026-10-17, `docs/04_herramientas/TOOLS.md`): voces ADX por personaje
   (15 000 por idioma) y textos de historia en 5 idiomas → voces de combate extra para los ports y frases de campaña.
   Modelos/mapas/guiones: formato o compresión sin identificar.
2. **Placas "sin texto"** (`hub/rango_NN_vacia`): ya se usan para "Maestro" en otros idiomas; sirven para rangos
   del online (Elo) cuando haya usuario propio.
3. **Fondos sin uso fijo**: Capsule Corp exterior (`fondo_retos`) también valdría para el Laboratorio si se quiere
   alternar con el de Dr. Gero.
4. **Bulma** (`hub/anfitrion_bulma`): generado y sin pantalla asignada (el Laboratorio va sin anfitrión por su tabla
   ancha); candidato para la tienda de Supervivencia.
