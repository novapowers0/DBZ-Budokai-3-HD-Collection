# User mods

This folder is empty and reserved for user-built mods. Mods live in
`mods/<name>/` and override AFS entries without touching the original game
files:

```
mods/<mod>/us/data_cmn.afs/<entry>/geom.bin   # override de una entrada del AFS
mods/<mod>/.disabled                          # si existe, el mod está OFF
```

Manage mods visually in the launcher (Mods tab) or with `mod center hd/`.
See `docs/02_mods/` for the full guide.

## Ejemplo: mod de música original (OG)

Para sustituir la música por la **banda sonora original (OG)** del juego,
crea un mod que reemplace los archivos de audio por región. Solo tienes que
colocar los archivos en la carpeta correspondiente del mod:

```
mods/<mod>/us/adx_jpn.afs   # audio JPN de la región USA
mods/<mod>/us/adx_usa.afs   # audio USA de la región USA
mods/<mod>/us/opening.sfd   # opening (si lo reemplazas)
mods/<mod>/us/Ending00.sfd  # ending
mods/<mod>/eu/adx_jpn.afs   # misma estructura para la región EU/PAL
mods/<mod>/eu/adx_usa.afs
```

El override de archivos completos (`.afs`, `.sfd`) se aplica por región sin
tocar los originales. Un mod de referencia es `og_music` (validado end-to-end).
Actívalo/desactívalo desde la pestaña **Mods** del launcher.
