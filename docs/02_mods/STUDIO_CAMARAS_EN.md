# Studio: redo technique cameras

The **Studio** is a Mod Kit window for editing technique cameras (ultimates, throws, hyper
mode). The game does not need to be open, and Blender is optional.

- Open it: Mod Kit → **"Camera Studio"** card (or, on a character with its own moveset,
  **"Cameras (Studio)"**). Also `python "mod center hd/studio/studio_gui.py" --lang en`.
- Format and technical details: `docs/03_formatos/CAMARA_ACC.md`.

## 1. Pick character and technique

- **Character:** the 38 in the game and the new characters with their own camera (`camara.bin`).
- **Technique:** each "Script" is a sequence of clips from the `#SPX` script (slot 0 = hyper mode /
  ultimate, 20 = throw). "All clips" shows the clips one by one.
- **First clip:** the script asks for clips as "base + K". The Studio estimates the base; if the
  marked clips (▸) do not match the technique, change it.

## 2. Look

- **Timeline:** one block per clip, with the script's wait; the grey band is the time the camera
  stays still (clip shorter than the wait). Red marks = hits (approximate).
- **From above / From the side:** orange = eye (the camera), blue = target (where it looks), grey =
  the character in that frame.
- **Roll and fov:** the clip's curves; click or drag to change frame.
- **Preview (toon):** the character in the game's style seen from the camera. "GIF" saves the
  animated clip to share it. The pose is a guide.

## 3. Edit

| I want to… | How |
|---|---|
| move the camera at one moment | drag the orange (or blue) dot in a view; "Smoothing ±frames" spreads the change to nearby frames |
| move the whole path | tick "move the whole path" and drag |
| exact values | "Values at this frame" → "Apply at this frame" |
| a new camera | **Templates**: Orbit, Dolly / zoom, Shake, Roll → "Apply to clip" |
| make it longer or shorter | "Length" → "Retime" (the script still waits the same) |
| go back to the original | "Undo" |
| copy another character's camera | "Copy…" |
| give cameras to a character without them | "Add" or "Copy…" (the script only uses the clips it asks for) |

## 4. With Blender (optional)

1. **"Open in Blender"**: opens Blender with the character and the camera (60 fps, frame 0 is the
   start of the clip).
2. Move the camera, change its fov, add keys… and save with **Ctrl+S**.
3. **"Bring from Blender"**: the Studio asks Blender (in the background) to export and reads the camera.

Rules: do not change the start frame; the end frame sets the length. Any 3D program that exports
glTF (`.glb`) works: "Export .glb…" / "Import .glb…".

## 5. Save and test

- **"Save as mod"** checks everything before writing (if something does not fit, nothing is written).
  - Game character: creates `mods/studio_<character>/` with the compressed camera and a
    `studio.json` (your edited clips; they load by themselves when you reopen the Studio).
  - New character: rewrites its `moveset/camara.bin`; it is mounted when you press PLAY.
  - The previous version is kept in `mods/<mod>/respaldo/<date>/`. Nothing is ever deleted and
    game files are never touched.
- **Test:** the first time, restart the game. After that, with the game open: Pause → "Reselect
  characters" → Training → use the technique. If the Studio warns that the camera grew past what was
  reserved, restart.
- To remove it: disable the `studio_<character>` mod in "My mods".

## Still to check in game

Camera orientation relative to the opponent and left/right mirroring; whether the script's wait and
the clip length match; whether "Reselect characters" reloads the camera.
