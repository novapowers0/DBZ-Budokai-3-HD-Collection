# Mod Kit: quick guide

The Mod Kit is a window for making DBZ Budokai 3 HD mods without typing commands.
It changes costumes and adds fighters, stages and techniques, and everything can be undone.

> Versión en español: [KIT_GUIA_RAPIDA.md](KIT_GUIA_RAPIDA.md)

**Note:** the Kit ships no art from other games. Anything from Budokai 1, Budokai 2,
Shin Budokai or Infinite World comes from **your own copy** (your ISOs). Anything marked "in testing"
passes the checks but has not been seen in the game yet.

## 1. How to open it

- If you downloaded the "Modding Kit" pack: double-click `DBZ3_ModKit.bat`.
- From the project folder: `python "mod center hd/modkit_gui.py"`.

Top right there are three switches: **Easy / Advanced**, **ES / EN** and **B3 HD / B3 PS2**.

![Kit home screen](img/kit/real_01_portada.png)

## 2. Easy mode and advanced mode

- **Easy** (the default): six big cards. Each one guides you step by step and shows you how it will
  look **before** anything is touched.
- **Advanced**: every tool with all its options, the hex editor and the full log.
  Only needed if you know what you are looking for.

Under the cards there are shortcuts: **My mods**, **Install a mod (.zip)**, **Story or challenge**,
**Music**, **Test in the game** and **Something is wrong** (it tells you what to do).

## 3. Change costume

Gives an existing fighter a new look.

1. Press **Change costume**.
2. Pick the character (for example, Goku).
3. Pick where the look comes from: **my model file**, **another character I already have**,
   **another costume from the game** or **just change colours**.
4. You see one box per form (Base, Kaioken, Super Saiyan...). Press one and pick its model,
   or use "the same for all forms".
5. Read the marks: ✓ fine, ⚠ it will use another form's model, ✕ missing, stays as it is.
6. Press **Next**, type a name and save.
7. Press **Build and try**.

![Change costume](img/kit/real_02_traje.png)

Status: **in testing** (works in the checks, not yet seen in the game).

## 4. Add character

Brings a fighter from another Budokai game in your collection.

1. Press **Add a character**.
2. Pick the source game (your ISOs are found automatically).
3. Pick the character.
4. Give it a name and pick the preset and the base character.
5. Press **Build and try**. It appears on the select wheel, after the stock fighters.

## 5. New stage

Adds a Budokai 2 or Budokai 1 stage as a new slot in the stage selector.
It does not replace any stock stage.

1. Press **New stage**.
2. Pick by photo (Budokai 2 and Budokai 1 tabs; photos come from your copy).
3. Check the label: "tested" or "untested". Stages marked "does not work yet" close the game and cannot be picked.
4. Press **Next**. Type a name (up to 15 letters) and a mood.
5. Press **Save**. It keeps a copy and you can **Undo**.
6. If the stage has a second zone, it is added on its own. To see it, turn on "B zones of new stages" in the game settings.

![New stage](img/kit/real_04_escenario.png)

## 6. New technique and animation generator

Changes the movement, camera and colour of a technique. Status: **in testing**.

1. Press **New technique**.
2. Pick who (for example, Goku).
3. Pick the technique (Kamehameha, Dragon Fist...). Names are the official capsule names.
4. Under **Movement** press **Open the animation generator**.
5. Find a move from another fighter in the library (search and filters) and watch it in the viewer.
6. Drop it on the track below. You can trim it, chain several pieces and set the blend between them.
7. Read the green checks: compatible bones, length and hits inside the clip.
8. Press **Save as technique** (keeps a copy), then **Test in the game**.
9. Under **Camera** pick a style (orbit, zoom in, tracking...). Under **Aura and ki colour** change the tone.

![New technique](img/kit/real_05_tecnica.png)
![Animation generator](img/kit/real_06_generador.png)

## 7. Fusions

Choose how long each fusion lasts: **Short**, **Like the original**, **Long** or **No limit**.
In advanced mode you can set exact seconds. Online matches are not affected.

1. Press **Fusions**.
2. Move the global selector or pick fusions one by one.
3. Press **Save**. **Undo changes** puts everything back.

Status: **in testing**. The game needs the new version to respect this setting.

**Gogeta as a standalone character** (in testing): turn on "Gogeta" and press **Save**; he shows on the wheel as one more fighter and starts already fused. It needs a free slot on the wheel; with many characters installed there may be none (then the Kit does not create him and tells you). Turn it off and save to remove him; **Undo changes** brings him back. Vegito and Gotenks: **not tested in the game yet**, they cannot be turned on.

![Fusions](img/kit/real_05b_fusiones.png)

## 8. Texture pack

Swaps a character's images for yours.

1. Press **Texture pack**.
2. Pick the character.
3. Press **Extract textures to PNG** and open the folder.
4. Edit the PNGs in your paint program. **Keep the same image size.**
5. Name the mod.
6. Press **Rebuild mod with edited textures**.

## 9. My mods, testing and undo

- **My mods**: see what you have, which are on, and turn them on, off or undo them.
  (Conflict warnings and a full mod check: **in testing**.)
- **Test in the game**: builds the mod, opens the game and tells you in one sentence whether it loaded.
  It presses nothing in the game and **your saves are not touched**. If the game is open it asks you to close it.
- **Undo**: every assistant has its button. New things are moved to the `modkit_backups` folder;
  nothing is ever deleted.

## 10. What cannot be done yet

- **Vegito and Gotenks as standalone characters** on the wheel: not tested in the game yet (Gogeta already works, in testing).
  Meanwhile they fuse in battle.
- In a copied move, **hits and damage are those of the move you replace**, not of the one you copy.
- **Cinematics with their own camera** (animated ultimates) cannot be moved with the generator.
  Neither can clips the game shares between characters, nor moves the character does not have.
- **Stages**: only from Budokai 1 and Budokai 2, and from **your copy**. Some still close the game.
- **Art from other games is supplied by the player.** The Kit does not hand out anyone's models, images or stages.

More help: [How to make mods](COMO_HACER_MODS.md) · [Texture packs](PACKS_DE_TEXTURAS.md) ·
[Model swap](MODEL_SWAP.md) · [Camera Studio](STUDIO_CAMARAS.md)
