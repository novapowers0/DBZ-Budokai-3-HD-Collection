# MIGRACIÓN TEMPORAL A CLAUDE — handoff y protocolo de retorno

> Documento de traspaso entre **opencode** (este repo, canónico) y **Claude**
> (migración temporal). Objetivo: trabajar en Claude **sin** perder trazabilidad y
> que **todo cambio vuelva a este disco como un único `.md`**. Claude **no** hace
> push a GitHub; el mantenedor aplica y sube desde opencode.

Última actualización: **2026-10-03**.

---

## 1. CONTEXTO MÍNIMO PARA CLAUDE (leer en este orden)

1. `AGENTS.md` — referencia operativa del proyecto (formato, constraints,
   comandos, invariantes). **Es la fuente estable.**
2. `MEMORY.md` — estado vivo (qué se hizo, qué falta, trampas, protocolo).
3. `docs/07_ports/SESION_DRAW_SEMANTICS_2026-09-11.md` — histórico de la Vía B;
   **§24** = último resultado (RE del skinning).
4. `docs/07_ports/UNIVERSAL_MODDER_2026-09-30.md` — método/RE con agentes.

**Naturaleza del proyecto**: port recompilado a PC de *DBZ Budokai 3 HD
Collection (Xbox 360)* con **ReXGlue SDK** (derivado de Xenia). La entrega
principal es el **swap nativo HD↔HD**; la Vía A (inyección PS2→HD) es aproximada;
la Vía B (port completo) está **aparcada** pero con un hallazgo RE nuevo.

---

## 2. 🟥 INSTRUCCIÓN OBLIGATORIA PARA CLAUDE (genera un handback `.md`)

> **CLAUDE: al terminar tu sesión, tu ÚNICO artefacto accionable es un fichero
> Markdown** llamado:
>
> ```
> HANDOFF_CLAUDE_<AAAA-MM-DD>_<tema-corto>.md
> ```
>
> Estructura **obligatoria**:
>
> 1. **Resumen** — qué hiciste y para qué, en 3-8 líneas.
> 2. **Cambios por fichero** — lista EXACTA de rutas **relativas al repo**
>    (creados / modificados / borrados). Por cada uno:
>    - Si es **nuevo**: el contenido completo en un bloque de código.
>    - Si es **modificado**: un `diff` unificado (`--- a/… +++ b/…`) o la función
>      completa antes/después. No describas: **muestra el cambio**.
> 3. **Comandos ejecutados** y su resultado (build, tests, scripts).
> 4. **No hecho / riesgo / supuestos** — qué quedó a medias, qué no pudiste
>    verificar, qué asumiste.
> 5. **Siguiente paso recomendado**.
>
> Reglas:
> - **NO** hagas push a GitHub. **NO** abras PR.
> - **NO** reescribas `AGENTS.md`/`MEMORY.md` salvo que el mantenedor lo pida
>   explícitamente para esta sesión; si propones cambios ahí, **descríbelos en el
>   handback** como texto a pegar, no los apliques.
> - Usa **rutas relativas** y **no incluyas** rutas personales (`C:\Users\...`),
>   claves ni datos de juego (`.bin/.afs/.awo/.xex/...`).
> - Si tocaste el SDK (`rexglue-sdk-0.10/`), recuerda que el build del juego
>   **sobrescribe** `rexruntime.dll`: hay que recopiar DLLs canónicas con
>   `tools\copy_sdk_dlls.ps1` (AGENTS §7).
> - Si tu trabajo es de **RE/instrumentación**, deja claro en el handback cómo
>   **revertir** y qué tamaños canónicos de DLL deben quedar (los vigentes están
>   en `AGENTS.md` §7, «DLLs canónicas del SDK 0.10»).

### 2.1 Plantilla vacía (Claude puede copiarla)
```markdown
# HANDOFF_CLAUDE_<fecha>_<tema>

## 1. Resumen

## 2. Cambios por fichero
### CREADO: ruta/relativa.ext
```
<contenido completo>
```
### MODIFICADO: ruta/relativa.ext
```diff
--- a/ruta/relativa.ext
+++ b/ruta/relativa.ext
@@ ... @@
```
### BORRADO: ruta/relativa.ext

## 3. Comandos ejecutados
## 4. No hecho / riesgo / supuestos
## 5. Siguiente paso recomendado
```

---

## 3. PROTOCOLO OPERATIVO (ambos lados)

### 3.1 opencode → Claude (entrega)
1. Cerrar la sesión local: `git -C github status` limpio + commit local.
2. Entregar `AGENTS.md` + `MEMORY.md` + este documento + estado de `github/`.
3. Fijar el **alcance** de la sesión de Claude (un objetivo concreto).
4. Recordar la **instrucción de §2**.

### 3.2 Claude → opencode (retorno)
1. Claude produce `HANDOFF_CLAUDE_*.md` (§2).
2. El mantenedor lo coloca en el repo (p.ej. `docs/handoffs/`) y lo revisa en
   opencode.
3. opencode aplica los cambios, actualiza `MEMORY.md` (estado) y, si toca,
   `AGENTS.md`.
4. `powershell -ExecutionPolicy Bypass -File tools\sync_github.ps1`
   (ejecuta `publish_check.ps1`; exit 1 = **no subir**).
5. Commit local → **push solo si el mantenedor lo autoriza**.

---

## 4. CHECKLIST DE CIERRE (para opencode)
- [ ] `HANDOFF_CLAUDE_*.md` leído y aplicado.
- [ ] `MEMORY.md` §1/§2 actualizados.
- [ ] `AGENTS.md` actualizado si el cambio es estable/invariante.
- [ ] `sync_github.ps1` → `publish_check.ps1` en PASS (o WARN aceptados).
- [ ] Commit local hecho; push pendiente de OK.
- [ ] Entorno de build limpio (sin procesos; DLLs canónicas; toml sin flags dev;
      artefactos RE de captura borrados).
