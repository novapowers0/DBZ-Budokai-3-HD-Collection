# Solución de problemas: 10 síntomas frecuentes

Guía corta para jugadores. Antes de nada: abre la pestaña **Avanzado** del launcher y usa
**Reparar instalación**; deja el juego cerrado. Si el problema sigue, busca tu caso aquí.
Los registros están en `user_data/dbz3/logs/dbz3_*.log` (o en `Documents/dbz3/...` si el
juego no puede escribir junto al ejecutable). Adjunta el más nuevo, la versión del launcher
y tu tarjeta gráfica.

## 1. No arranca o se queda en negro
- Comprueba que `default.xex` y las carpetas `us/` o `eu/` están junto a `dbz3.exe` (o elige
  el `.iso` en el launcher).
- Prueba el otro backend en **Vídeo** (D3D12 / Vulkan) y actualiza el driver de la GPU.
- En el log busca `device removed`, `DRED` o `UNASSIGNED`; si sale, baja la escala interna a
  `1x`, quita MSAA y la mejora de texturas.

## 2. «No se detecta el ejecutable»
- El launcher busca por tamaño y MD5; no hay que renombrar nada. Deja `default.xex` en la
  raíz de la carpeta del juego (o en `DBZ3/`, `assets/`, `assets/DBZ3/`).
- Si usas una ISO, selecciónala en el launcher; se extrae solo el ejecutable a la caché.
- Un xex de Budokai 1 o el menú HD se rechazan a propósito: no son el juego.

## 3. Va a 30 FPS o da tirones
- El coste real es el **supersampling**: deja **Escala interna en 1x** (los presets no la
  suben). A 2x/3x la GPU dibuja de verdad a más resolución.
- Desactiva la mejora de texturas (`dbz3_hd_textures = Off`) y MSAA para probar.
- Con vsync, si un cuadro pasa de 16,7 ms el juego cae a 30. Mira `perf fps=` en el log.

## 4. No se oye nada
- Sube **Volumen** en la pestaña Audio y quita **Silenciar**.
- El juego se silencia al perder el foco: desactiva `dbz3_mute_unfocused` en **Avanzado**.
- Si usas varios dispositivos, cambia el predeterminado de Windows y reinicia el juego.

## 5. El mando no responde
- En **Controles** elige XInput (recomendado) o SDL. Con SDL se usa
  `gamecontrollerdb.txt`, que va junto al ejecutable.
- Si el teclado funciona y el mando no, prueba otro puerto USB y cierra RTSS/OBS.
- El teclado es el modo por defecto (`mnk_mode`); pulsa una tecla para despertarlo.

## 6. Un mod no aparece o el juego se cierra al activarlo
- Activa **un solo mod** para descartar choques entre mods y pulsa **Refrescar**.
- Comprueba que el mod es para esta versión; un mod viejo puede quedar desactivado solo.
- Los mods no se aplican en modo ISO: usa la carpeta del juego para mods.

## 7. «Aplicar» la partida al 100 % sale en gris
- Falta la carpeta `mods_nativos\partida_100` junto a `dbz3.exe` (con `data.bin` y
  `DBZ3.header`). Vuelve a instalar el paquete completo; no basta con copiar el `.exe`.
- Cierra el juego antes de aplicarla: el launcher guarda una copia y permite restaurar.

## 8. No salen los escenarios convertidos (B1/B2/IW)
- En **Juego**, revisa la fuente del juego y usa **Reparar instalación**; el conversor
  necesita la ISO o la carpeta original de PS2.
- Si la conversión se quedó a medias, vuelve a lanzarla desde el Kit y espera al mensaje
  final (no cierres el launcher mientras convierte).

## 9. Cuelgue o «device removed» en combate
- Baja escala/MSAA/texturas HD y prueba otra vez; suele ser VRAM o driver.
- El log de DRED nombra la cola y la lista: adjúntalo tal cual al abrir un issue.
- Si pasa solo con un personaje añadido, indica el mod y el personaje.

## 10. Todo sale en inglés o faltan textos
- Cambia **Idioma** en la esquina del launcher (ES/EN/IT/DE/FR/JP). El juego usa ese idioma
  al arrancar; algunos textos de mods propios pueden seguir en su idioma original.
- Si el idioma no cambia tras guardar, cierra el juego y vuelve a abrir el launcher.

---

Nota interna: borrador en español para la guía X3.8 (10 síntomas). La traducción a los otros
seis idiomas queda pendiente; las claves nuevas deben entrar en `src/launcher/i18n.cpp`.
