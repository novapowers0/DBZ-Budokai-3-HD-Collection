# HOJA DE RUTA — Model swaps, costumes y roster

> Actualizado: 2026-08-14. Plan estratégico de 3 fases. La base de todo es
> entender el modelo a fondo (fase 1), luego costumes (fase 2), luego roster (fase 3).

---

## FASE 1 — MODEL SWAP FUNCIONAL (entender los modelos en profundidad)

### Objetivo
Conseguir un model swap funcional HD→HD (ej: Goten en slot de Krillin) para
entender EXACTAMENTE cómo el guest parsea y valida los bins.

### Estado actual
- ✅ Override por entrada funciona (el bin se sirve íntegro).
- ✅ Los 3 fixes del pipeline están aplicados y documentados.
- ✅ Tenemos `analyze_bin_hd.py` (parser con template oficial).
- 🔴 El guest crashea al procesar un bin de otro personaje (contenido, no mecanismo).

### Hallazgos que guían la fase 1
1. **El guest NO valida los magics** (#AWO/#AWG no aparecen como constantes en el
   código guest) — confía en el índice AFS y lee el header por offsets.
2. **El stride 44 del vértice está confirmado** en el guest (`mulli r11,r11,44`).
3. **La comunidad no tiene conversor PS2→360** — nuestro salto es único. El método
   HD que documentan: descomprimir → 010 Editor + template B3_AMB → recomprimir.
4. **El crash es por CONTENIDO**: el bin se sirve íntegro (got=106496) y aun así
   crashea → el guest rechaza la estructura del bin de Goten.

### Plan de la fase 1 (en orden)
1. **[EN CURSO] Instrumentar el runtime** para loguear el **PC guest** donde se
   procesa el bin 327. Comparar el flujo del bin original (funciona) vs Goten
   (crashea) → ver la dirección exacta de divergencia.
2. **Identificar el parser guest real**: con el PC guest, localizar la función en
   `generated/dbz3_recomp.*.cpp` y leer SU lógica de parsing.
3. **Aislar el campo que crashea**: con el parser identificado, comparar
   campo a campo el bin de Krillin vs Goten → saber qué valida.
4. **Probar swap con bins del MISMO esqueleto**: hay personajes del B3 HD con
   estructura casi idéntica a Krillin (misma pose). Si un swap funciona, el
   formato está validado.
5. **Convertir PS2→HD correctamente**: con el formato entendido, aplicar el
   re-layout AMO0→AWO (AWO_FORMAT.md) a Janemba.amb.

### Herramientas para la fase 1
- `rexglue-sdk/src/filesystem/devices/host_path_file.cpp` — instrumentar aquí
- `generated/dbz3_recomp.*.cpp` — el parser guest real
- `awo_tools/analyze_bin_hd.py` — analizar bins
- Tracy (build win-amd64-tracy) — profiling

---

## FASE 2 — TRAJES ADICIONALES (costumes sin perder slots)

### Objetivo
Añadir trajes extra a personajes sin perder slots ni romper el juego.

### Datos clave
- **Cada personaje ya tiene slots de outfit alternativos**: Krillin tiene
  327 (normal), 328 (Buu Saga), 329 (Namek). Son bins separados.
- La comunidad añade trajes **reemplazando slots de outfit alternativo** o con
  SLXS (Lesson 1-2 "Adding models to costumes").
- El SLXS del B3 HD es el archivo que mapea personaje→trajes.

### Plan de la fase 2
1. Mapear los slots de trajes de todos los personajes (del AFS).
2. Verificar cómo el SLXS asigna trajes a personajes.
3. Probar: duplicar un traje de Krillin en un slot vacío → ver si aparece
   como traje seleccionable.
4. Si funciona: el pipeline para trajes nuevos = slot libre + bin editado.

### Recursos
- `modding resources discord\tutorials\SLXS...` (lesiones 1-2, 2-2)
- `mod center\SLXS Editor v0.50`
- `All_Character_Slots.txt`

---

## FASE 3 — AÑADIR PERSONAJES AL ROSTER

### Objetivo
Añadir personajes nuevos al roster (duplicando uno existente) sin perder a nadie.

### Datos clave
- En PS2, el roster se edita vía SLXS (bloques de personaje + pantalla de select,
  Lesson 4-1) o cheats para ocultos.
- **En la HD Collection no hay ningún ejemplo público** — seremos los primeros.
- El plan del usuario: **duplicar un personaje existente justo debajo de otro**
  (usar un slot vacío del AFS) y cambiarle el modelo/la cara.

### Plan de la fase 3
1. Identificar slots vacíos del AFS (o cómo duplicar una entrada).
2. Entender el SLXS del B3 HD (cómo mapea roster → personajes → bins).
3. Duplicar un personaje (ej: Krillin) en el slot nuevo con bin propio.
4. Añadir una cara nueva (textura select) al personaje duplicado.

### Recursos
- `mod center\SLXS Editor v0.50`
- `modding resources discord\tutorials\SLXS Edit Tutorial - Lesson 4-1`
- `DBZ_B3_Character_Bin_List.txt`

---

## PRINCIPIO RECTOR

> **No adivinar el formato — leerlo del guest.** El código recompilado en
> `generated/` es el parser REAL. Cada campo del bin está validado por una
> instrucción del guest. Instrumentando el runtime (loguear el PC guest) podemos
> ver exactamente qué lee y valida el parser, y adaptar nuestros bins a eso.
> La documentación de la comunidad sirve como guía, no como verdad última —
> ellos trabajan PS2, nosotros el único HD con recomp.

---

## REFERENCIAS RÁPIDAS

| Tema | Dónde |
|---|---|
| Pipeline de mods (override) | `docs/02_mods/COMO_HACER_MODS.md` |
| Investigación model swap | `docs/02_mods/MODEL_SWAP.md` |
| Formato bin | `docs/03_formatos/` + `AWO_FORMAT.md` |
| Herramientas | `docs/04_herramientas/TOOLS.md` |
| Compilar | `docs/05_build/COMO_COMPILAR.md` |
