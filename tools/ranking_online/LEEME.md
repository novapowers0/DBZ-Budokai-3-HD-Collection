# Ranking online (RANK-ONLINE): qué hay que hacer en GitHub

El juego envía la partida firmada a un buzón de ntfy.sh y descarga las tablas de este repo. Una GitHub Action lee el buzón
cada 10 minutos, comprueba la firma y los puntos y reescribe las tablas. El jugador no abre el navegador ni crea cuentas.

Archivos de esta carpeta:
- `validar.py`: lo que corre la Action (firma, duplicados, plausibilidad y tablas). Autoprueba: `python validar.py --autoprueba`.
- `ranking.yml`: el workflow de la Action.
- `clave.py`: crea la clave HMAC. Ya está creada en `E:\DBZ3HD_trabajo\ranking_online\clave\RANK_HMAC_KEY.txt`
  (y compilada en el juego desde `src/ranking_clave.local.inc`, que **no se sube**).
- `servidor_falso.py`: solo para las pruebas en local.

## Pasos (una vez)
1. Crear en la cuenta `novapowers0` el repo **público** `DBZ3HD-Ranking`, con un README (para que tenga la rama `main`).
   El juego descarga de `https://raw.githubusercontent.com/novapowers0/DBZ3HD-Ranking/main/`.
2. Subir al repo:
   - `validar.py` en la raíz;
   - `ranking.yml` en `.github/workflows/ranking.yml`.
3. **Settings → Secrets and variables → Actions → Secrets → New repository secret**:
   nombre `RANK_HMAC_KEY`, valor = el contenido de `E:\DBZ3HD_trabajo\ranking_online\clave\RANK_HMAC_KEY.txt`.
4. En la misma página, pestaña **Variables → New repository variable**: nombre `RANK_TEMA`, valor `dbz3hd-rank-c604011f9edf`
   (el tema del buzón; es el valor de serie de `dbz3_rank_tema` en el juego).
5. **Settings → Actions → General → Workflow permissions**: marcar **Read and write permissions** y guardar.
6. **Actions → ranking → Run workflow** una vez: crea `survivors/`, `rogue/diario/`, `recibidos.json` y `datos/entradas.jsonl`.
7. En el juego: Ajustes rápidos → Modos → **Ranking online (beta)**, o `dbz3_ranking_online = true` en `dbz3_user.toml`.

## Al sacar una versión nueva
- `python tools/ranking_online/clave.py --nueva` antes de compilar la versión: la clave anterior queda en
  `RANK_HMAC_KEY_ANTERIOR.txt`.
- En GitHub, el secreto `RANK_HMAC_KEY_ANTERIOR` pasa a ser la clave vieja y `RANK_HMAC_KEY` la nueva. Así valen las dos versiones.

## Límites comprobados (12/11/2026, documentación oficial)
- ntfy.sh, sin cuenta: 250 mensajes al día por IP, 60 peticiones seguidas y luego una cada 5 s; mensajes de hasta 4.096 bytes
  (el del juego mide unos 400); los guarda 12 h; `?poll=1&since=12h` devuelve lo guardado y cierra. Cada jugador envía muy
  pocas partidas al día: sobra.
- GitHub Actions en un repo público: gratis. El cron va como mucho cada 5 min y puede retrasarse cuando hay mucha carga (por eso
  corre en los minutos 7, 17, 27...). **Si en 60 días no hay ningún cambio en el repo, GitHub apaga el cron**: cada partida
  recibida hace un commit, así que solo pasa si nadie juega; se reactiva con un clic en Actions.
- Si la Action tarda más de 12 h, ntfy borra el mensaje: el juego lo reenvía solo (cada 6 h) hasta verlo en `recibidos.json`.

## Si algo falla
- Tabla vacía en el juego: mirar en Actions el último "ranking" (si sale "falta RANK_HMAC_KEY", falta el paso 3).
- Todo "Rechazada": la clave del juego no es la del secreto (paso 3) o la versión es de otra clave (ver "Al sacar una versión").
- Plan B (si ntfy deja de servir): un Cloudflare Worker con la misma firma; en el juego solo cambia `dbz3_rank_url`.
