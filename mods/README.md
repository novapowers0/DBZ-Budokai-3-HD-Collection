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
