# Changelog

All notable changes to **DBZ Budokai 3 HD Collection** (PC port). Older versions
are summarized at the end; the full history (in Spanish) is in
`docs/01_estructura/HISTORICO_RELEASES.md`.

Downloads: [latest release](../../releases/latest).

## v1.5.6 (date pending)

### Last additions

- Hero mode (beta): an **Export** button in the creator saves your hero as a separate character (look, forms, moveset, ultimate) with an art-free `receta.toml` to share; the model is never shared. A recipe is rebuilt with `vista_heroe.py --receta` from the game of whoever has it (no in-game import yet); without XV2 the body falls back to B3 parts. PC only. Master techniques and progress do not travel. Its name on the select wheel, a transformation and its ultimate were seen in tests; a special move was confirmed by a battle probe, not on screen.
- Dragon Tactics (experimental): figures keep their character's real proportions and gain hit, knockdown, get-up and victory animations from the game's shared bank (checked on Goku and Broly; the others are assumed from their shared bone names). FIGHT! banner, sell and hover highlights. A hit no longer sends a walking figure back to its starting tile.
- Mr. Satan legend (beta): he always has High Tension, Dynamite Kick, Rolling Hercule Punch, Hercule Special and Present For You, on every difficulty (his list does not depend on it; checked on Novice and Expert). His ultimate was seen hitting. High Tension raises his form in the log (it needs 4 ki bars and his model does not change).
- Budokai 2 board (experimental): Shenron's wish uses his model with long whiskers. Dragon Balls show as orange ellipses without stars, the belly has fewer stripes than the original and the light column stays off.
- Online (beta): the screen now starts by choosing 1 vs 1 or Tag Team, then your team of two and a room with a code to copy, with plain messages and mouse or controller. Tested with two copies on one PC, not two real PCs: a full Tag Team pairing passed with 0 differences through the test route, which skips these screens. In Tag Team online each player fights with their own Custom list (the Normal one if a fighter has none), sent to the other PC when the room connects; the team is still picked on this screen, not on the wheel. The same goes for online 1 vs 1: each side's saved Custom lists for the 38 base slots are sent when the room connects; added characters use their Normal list in 1 vs 1. The network protocol version went up, so builds from before cannot join. Two rooms with different Custom lists and real swaps ended with 0 differences.
- Known, not fixed: mods using `formas_donante` (Goku GT and Vegeta GT from IW, Saiyan Vegeta, Goku Black) may not fire E+direction specials; the cause is narrowed down but not fixed.
- Known, not fixed: with experimental rollback manually enabled (off by default, no in-game or launcher option), one paired run closed the game in a long fight. A 6,600-tick lockstep fight (the default mode, two copies on one PC) had no crash or checksum difference. Rollback stays off.
- Known, not fixed: Dragon Rush problems reported on a Kit-made Goku (#66, #67) could not be reproduced with a matching setup yet.

### Dragon Tactics (experimental)

- New auto-battler in Dragon HUB on the World Tournament stage: a tournament against seven bots with pairings, eliminations and a final ranking. The roster has **34 Budokai 3 fighters and 11 lineage/class synergies**, a five-card shop, refresh, EXP, bench, selling, nine capsules (up to two per unit), unit cards and teams of three to six. Only Budokai 3 HD data is required.
- Units can reach four stars, showing their next B3 transformation where available (Goku base → Super Saiyan → SS2 → SS3, for example). Fighters with fewer forms keep their last model and gain strength. Four stars requires 27 copies. Freezer starts in his second form, Cell in his imperfect form, and Vegeta reaches Majin at three stars. Incompatible older beta saves show a notice and start a new match.
- Lock the shop, earn streak gold, collect gold and a capsule in neutral rounds, inspect another board, team damage and the last duel. The mode has a start menu, saved options and matches, controller/mouse support, 16:9/4:3/21:9 layouts and an optional preparation timer (off by default).
- Clearer board layout, outlines on bright backgrounds, shop odds beside EXP, star-upgrade hints and synergy counts. Hits briefly glow rather than hiding a fighter. In the measured 540-fight sample, target switches fell from 760 to 55 and display-push jumps from 496 to zero; fighters can still get trapped behind allies.
- Character voices use the game's own yell banks (up to 11 voices per round); European data is supported. Corrected walk-clip choices, standing selection poses for Yamcha/Ginyu where their normal idle is unavailable, and animation-cache refresh after manifest changes. Ki projectiles have a measured glow improvement. Technique-specific projectile textures remain unfinished; the unverified beam effect is not enabled (see the 1.5.6 additions below for purchase sounds and orb colours).

### Characters, costumes and technique labels

- More than 24 extra characters use L1/R1 select pages, tested with 122 characters in Duel, Tag Team and Edit Skills. Saves load with and without mods. The final short page repeats fighters from earlier pages to avoid a cursor crash; the underlying cause is still being investigated. In a separate 120-character test, startup fell from over 120 seconds to 70.7 seconds (71.7 seconds without those mods).
- Characters can use more than 64 resources (fixing Vegeta GT's exit crash), and transformed forms retain their own movesets, scripts and techniques. Added costumes are selectable when some native costumes remain locked; cycling now skips the locked native entries instead of hiding the extra costume.
- Infinite World imports use their own ultimate scripts, cameras and targets. Gogeta, Vegito and Baby finish their ultimates (#58/#57); transformations keep techniques and ultimates (#54), failed fusion transformations no longer loop, and an end-of-fight IW Gogeta crash is fixed. Goku GT's connecting ultimate no longer crashes, empty effect rows no longer cause flashes, and base ki bars are read from the source data, including already-imported characters (Baby 5, Super 17 5, Janemba 6, Pikkon 4, Vegito 6–7 and Vegeta GT 3–4–6).
- Gogeta SS4 can be imported separately with his own moveset and ultimate. Another Road is recognised by its contents, including renamed ISOs and the European “Shin Budokai 2”. Shin Budokai belt tails hang rather than sticking out.
- Fixed Shin Budokai 1 skin references: all 18 SB1 models now have 1,064–1,552 linked references instead of zero; the 24 SB2 models remain readable. SB imports use the native B3 HD model with SB moves where a native equivalent exists; Future Gohan, Janemba and Pikkon retain their SB bodies. Krillin, Vegeta and Janemba SB1 were checked in battle; the other characters were checked offline. On Play, older SB1 mods and eligible SB2 mods rebuild from their recipe when the PSP ISO is available, keeping a backup. Hand-edited capsules/model files are not carried into the rebuilt mod (the backup retains them). Ultimates with the newly selected bodies remain unverified.
- Rebuilding the roster fills missing battle labels for default/form capsules, inherited techniques and the extra Buu absorption techniques. Native labels are preserved. An audit of 29 imported characters found zero missing battle/menu/skill labels in six language banks; the rebuilt Buu bank was inspected in English, Spanish and French. Generic names now include the character name, but are not claimed to be the original official Shin Budokai technique names. Krillin SB1's label was seen in battle; Buu absorption labels and a second language still need an in-game check. New repeatable tool: `mod center hd/auditar_nombres.py`.
- Zarbon/Dodoria right-direction specials were photographed during impact; their ultimate sequences were inspected without visible T-pose. Dodoria was rechecked with his actual imported ID against native Nappa: the dummy's zero-damage ultimate reading also occurs natively. The five reviewed added fighters were observed with their feet on the ground. These checks do not certify every special or original-game parity.
- B1 conversion now scales ultimate cameras and script placements to character height (`cam_escalada`); updated Zarbon/Dodoria camera bins were visually judged better. Some shots still show empty sky, a distant fighter or cropped faces. Android 19 has not received this camera adjustment; no synchronized B1 comparison was made.
- Separately prepared Zarbon, Dodoria and Burter mods contain 16/12/9 selected T3 voice slots as local 24 kHz WAV files, with silence in the remaining slots. The semantic choices and one rendered WAV per character were checked by local transcription; playback was not heard in game. These WAV files are not shipped in the repository or game archive, require no new executable and are not claimed to have been published. This supersedes the earlier withdrawn duration-only assignment.

### Infinite World / Dragon Mission (experimental)

- More original intro/result animations, cameras and durations; HUD suppression during those scenes, CLEAR!/FAILED! signs, held final poses, complete QTE/Kienzan endings and less premature clipping. Gohan's Training, Piccolo's button/target art and antenna effect, flight goal/win/loss scenes, spaceship success and the NB013/NB014 round rules received targeted corrections. These are incremental fixes, not full visual parity across all 18 minigames.
- On-foot walking/running, dash-wall collisions, Namek rocks/life bar, result tables over the scene and original READY/START cards; corrected ground height so fighters are not held above the real floor. Flight turns, ring placement and effect attachment, combo zoom and the Piccolo/Cell result wait follow more source timing. Effects can use real additive blending. The extra wall-clock projection shake was removed after its source command was identified as controller vibration.
- Random Dragon Ball rewards after wins (repeats possible), map shake when unlocking missions, difficulty passed to battles, source-based capsule tray and random Shenron wishes with scattered Balls. Missions that support character choice use that character; unavailable alternate minigame animations are reported rather than silently substituted. B3 HD bodies remain the base, with IW animations; Bubbles, the car and first-person hands use their required objects.
- Latest measured corrections include READY rotation/elevation in 7D2, HUD positions in 7D3/7D7, the left-hand table and rounds 2/3 in 7DA, connected charge stages and Piccolo clips. The 7D2 sign-change residual improved from 33.693 to 19.074 per 1,000 samples, versus 7.080 in the original, so camera parity is still open. Hit/fade timing uses the original 17-frame animation; red damage tint/reaction, a Freezer result pose, Bubbles/lava/Kikoho paths and AST02/AST07 effects remain unresolved. The five newly sampled clip sets had no duration mismatch among their common clips; that does not establish whole-scene parity.
- Freezer and Krillin retain their own original 7D7 height rather than being supported on B3's sea plane: 471/471 samples of each were within two units of their own height. Freezer's measured median residual was −0.057/−0.307 in the two sampled states. This does not certify the full original dodge motion.
- CapsulePick/Shenron capsule-scene parser, audio and rendering work remains an isolated test route, with VU/GS/material paths still blocked. It is not announced as a completed production scene or complete SPU2/GS reproduction.

### Dragon World / Babidi's Ship (Budokai 2, experimental)

- The saved Zenie wallet persists with the source game's 99,999,900 limit. Board comic panels, help strips, capsule cards, intro skipping/timing, random one-to-three Shenron wishes and menu/plane transitions received source-based corrections. Shenron's background was corrected from pale grey to a dark gradient, still a little lighter than the reference; his framing, whiskers/body artefacts, pose and particle effects still differ.
- Ship menus no longer remain black during chained-battle waits. The wheel remembers each side's fighter/costume/list, supports quick select and source-style costume changes, and uses corrected confirmation, bubble and wheel transitions. Floors II–IV use battle-time Kili rewards, the floor IV time-out draw result, gold reward bar and source help buttons. Result pauses use 41 PAL fields while preserving the 60 Hz US path. Backing out of the COM wheel in single player returns to the trial screen.
- Removed the extra VS card before ship battles (the shared change was visually checked on floor III). **All four floors are not certified end to end against the original:** their floor-specific battle counters/HUD and original KO/result comparisons remain open; the port still shows B3's normal battle display. The floor I survival test now correctly accepts another chained opponent rather than waiting for a result screen.
- Large B3 HD figures are drawn in batches instead of dropping outlines/distant parts, with roughly four times faster CPU drawing, no second shading pass on baked materials and an HD portrait fallback with a logged reason. Tools can find game data when settings are under `user_data/dbz3` or Documents. Board figures use source-model heights rather than forcing children and adults to the same height. Unanimated Saiyan shoulder/skirt bones no longer retain their flat B3 rest pose; Satan's camera/game props are omitted. The 37-figure sheet was inspected, without original-game photo parity. Tenshinhan's wide trousers are not a confirmed defect: the original wheel hides his legs.

### Mr. Satan legend (beta)

- Arrival car projection, departure frame, widescreen visibility, smoke timing and Yamcha's opening voice timing are closer to the source. READY shots in rounds 2–11 use the original zoom ramp; the round summary's faces, box and bottom sign are no longer misplaced/cropped.
- The opening fade lasts two seconds rather than about one sixth, covering subtitles correctly. Smoke uses source affine scaling and perspective-correct sampling. Satan's back-facing pose was ported to the HD skeleton with a measured limb-angle residual of at most 0.30 degrees; the final measured summary-box position residual was at most one pixel. These are targeted comparisons, not pixel-identical scenes.

### Hero mode and optional imports (beta / experimental)

- Name your hero in dialogue and selection, avoid repeated dialogue in consecutive fights, and find marked training, optional battles, items, Zeni and Dragon Ball events on each saga map. Missing saga Balls are awarded at saga completion. Shenron's native scene ends the story; the selected wish row reopens race/body and returns to the menu normally.
- Real story prizes show a capsule card; the empty grey capsule no longer appears after every battle. Thirteen more glove pieces were repaired without changing the 38 previously approved pieces. Mod-added technique descriptions, migration to the three-slot save format, a B3 base-hero preview fallback and visible build errors improve the creator.
- Optional Dragon HUB artwork is supported in creator/master screens through a separate pack. Story translation is partial: reply/victory/map/master templates now have French/German/Italian text, checked in generated data rather than on screen. Saga introductions, endings, titles and encounter lines in `sagas.toml` remain English; Portuguese/Japanese native Dragon Universe dialogue falls back to English.
- Optional Xenoverse 2 conversion reads **YOUR installed copy**, with Goku/Vegeta/Piccolo battle checks, facial/hand poses and local disabled mods. Optional hero hair/faces/clothes keep B3 parts as the default. Improvements cover eyes, pupils, hair lines, head/neck placement, closing hands, glove/sleeve hiding and a sharper/lighter preview. The last Namek skin adjustment was checked with Piccolo, not every race; donor height and approximate skirt/hip fitting remain limitations.
- Tagged XV2 imports can use experimental local spring/collision physics, measured with Goku in US/EU battles. This is a reconstruction with no dynamic normals; the hero-created XV2 path is not validated through it. No XV2 assets are shipped.
- New COLLADA/DAE reader and B3 costume converter reuse the XV2 conversion path. The extra-costume fix was measured with a locally generated Piccolo Red Ribbon disguise through the real selector and battle, including the left forearm. Masked mouth motion remains unverified. This note announces converter code and the selection fix, **not a downloadable third-party character or permission to distribute its artwork**.

### Music, interface and Modding Kit

- Twenty Tenkaichi 3 battle tracks can be read from **YOUR copy**, with a remembered fixed-track choice in F1/launcher or L1/R1 at stage selection; original music is retained. A local-recipe music card and ISO picker support that source. The Kit's Add Music assistant creates switchable mods from mp3/wav/ogg/flac/adx.
- The Kit has step-by-step assistants, plain-language errors, Build and Test and an Expert area. New B3 PS2 mode makes texture, model-swap and looped-music mods into a **separate ISO built from YOUR copy**. Select/battle checks used US SLUS-20998 in PCSX2; other regions and consoles remain untested. The PS2 preview explicitly uses the HD model. Help, image/texture/model previews, audio playback and waveforms are available; capsule/aura previews are illustrative.
- The Kit's Optional art page checks a folder of the player's PNGs (names, shape, transparency and damaged files), then installs recognized HUB pieces as a switchable mod, backing up replaced images for Undo. DAE helpers and art tools are included as code; third-party artwork is not. Normal B2 imports and Kit roster builds already apply the shared shading converter. The launcher header no longer shows the Dragon HUB logo.
- New Tactics hints, last-duel text, music/online notices, recipe cards and ISO notices have seven-language interface coverage. Fifty earlier news texts gained IT/DE/FR and PT/JA entries; 52 Nova names/descriptions were covered separately from the static key test. German “Boo”/“Golden” inconsistencies were corrected. The translation-key check passes; this is not visual acceptance of every language/layout. Hero saves, shader-cache copies, tutorials, Tag Team faces, images and colour previews now log failures more clearly.
- Repair installation checks `librexruntime.so`/`librexgpu-xenos.so` on Linux instead of reporting Windows DLLs missing, omitting DirectX 12 there (#62). The SDL controller-database notice remains informational. The platform branch was verified in source; Linux runtime testing remains outstanding.
- Optional native code plugins (issue #23), off by default: DLLs under `plugins/` can use `rex_mod_init` with the memory base, function lookup, region, version and logging; loaded plugins are logged. See `docs/02_mods/PLUGINS.md`.
- Windows HD-video playback is optional and downloaded separately (about 287 MB); the source regional soundtrack and original credits are retained. Resume, SHA-256 checks, fallback to originals and removal to backup were tested. Linux retains original videos. A real played story ending and modest-PC performance remain unverified (a forced ending was seen; see the 1.5.6 additions). No third-party video author's name is newly added here pending publication permission.

### Online (beta) and packaging

- Paired tests with more than 24 added characters reached logical page 2, selected IDs 57/58 and completed battle in both logs. Rollback was off and no checksums were compared in that test. Per-player music and the local-only notice were checked in a pair. The lobby identifies different operating systems instead of the generic version error (#51). These tests do not certify transformations under rollback or universal online compatibility.
- Rollback remains experimental/off by default. The coordinator recorded an earlier 50/50 paired acceptance with its identified executable. CMake now restores the write-log include when configuring regenerated US code; a clean-build/runtime check of that path, the final integrated executable, Buu absorption determinism and clearing selection pins between rooms require final acceptance. Those earlier results are not a new 1.5.5.6 certification.
- The release tools include local Nova recipe code/imports, XV2/DAE helpers, Tactics data and portable Python with SciPy/Tk. Cellin requests Budokai 1; Kuriza, Majin costumes, Buu absorptions and fusion recipes request Budokai 2. Source-availability checks and portable imports passed in isolation.
- Nova's latest report saw Kuriza, Majin Freezer/Cell, Cellin, extended Buu absorptions and Yamhan/thin Yamhan/Gotan in battle. Fusion is partially delivered: Gotan and Yamhan were seen in battle with injected input, but the real button sequence was not seen (see the 1.5.6 additions). The expanded absorption only activates on a connecting ultimate (11 deliberate misses did not absorb). A wrong technique-slot lookup was fixed; Vegeta/Cell/Freezer/Ten+Yam inherited B3 ultimates ran without closing the game. B3 moves/techniques are inherited, but voices were only checked in registry data, machine use is incomplete and mouths/antenna/face marks remain unresolved. The source-ISO card was viewed with and without ISO and Create Kuriza completed. Selection and Edit Skills remain unverified; no upload is claimed.
- Game ISOs, extracted models/audio/videos, generated character mods and the optional HUB artwork pack are not in the game archive. Existing own UI/Tcl images and historical save/converter-ramp exceptions remain.

- Revised Online BETA panel, with consent before any service request and optional compatible search. Two local copies paired in 9 s against a fake ntfy service; cancellation, outage and incompatible topics were tested. No two-PC test. Service limit: 250 announcements/day/IP (about two hours of searching); a failed-service wait measured 40 s. The panel was seen in 4:3 and 21:9.
- Joaofury's Ultra Instinct Goku beta has permission, a prepared catalogue entry and a seen battle model; separate publication is still pending. The transformation wheel and aura were seen; voices were not recognised in an audio dump (see the 1.5.6 additions). The adaptation uses a recoloured Goku aura and duration-assigned yells rather than the author's original banks. The reported Cooler knockdown/recovery problem is not reproduced or diagnosed; a ring-out smoke test does not close it.

### Additions in 1.5.6 (measured; nothing here claims original-game parity)

- Interface: the Select Characters "Normal" box covering the title at 16:9 and 21:9 no longer reproduces (checked with Goku); the 4:3 bands (Kumite, Dragon HUB) no longer show leftovers of another menu or the KYU bar over the Tactics text; the What's New window scrolls.
- Dragon Tactics (experimental): drag-and-drop between bench and board with a placement preview (6/6 press/release pairs logged, mouse test passed). Target choice uses real path steps: units idle for 8 s fell from 12 to 4 and back-and-forth moves from 194 to 94 in 20 tournaments; Regenerators were retuned from 64.2 % to 57.8 % wins. Buy/sell sounds were measured in an audio dump but not heard by a person. Beams and orbs take colour and, where available, texture from the game data in your copy.
- Hero mode (beta): BETA banner on the six creator/story screens and in the Kumite list; the native scouter is drawn with its own material (glass colour is a known gap). A forced seven-Ball ending went Shenron, credits with the HD video, title and menu without crashing; a real played ending was not run.
- Fusion: Potara (Gotan) now appears in Edit Skills, as do mod capsules in general (the editor required the wrong address range). A failed Yamhan fusion no longer closes the game: a clip missing from a mod without its own moveset is skipped and logged. Yamhan S gets the Budokai 2 Yamhan moveset, so installed recipes must be rebuilt. Not seen: the minigame with the real button sequence.
- Majin Buu absorption: with the ultimate capsule equipped, the post-absorption technique and its label were seen in battle.
- Gogeta SS4's ultimate no longer cuts out (previous form's animation banks are relinked): 11 of 11 ended with no damage before; afterwards the whole chain played (1,820 damage). Vegito's ultimate also plays in full.
- Added-character voices: Jeice and Guldo mods carry 16 and 19 selected Tenkaichi 3 lines as local WAVs; which line plays on which move is not confirmed. Nothing is shipped in the download.
- Online (beta), on one PC: lockstep with 120/200 ms latency, jitter, up to 20 % lost inputs and a slow-PC emulation kept every checksum equal; rollback (experimental, off by default) ended 25/25 in the final paired run. Matchmaking via ntfy.sh paired two copies in 7 s. Not tested: two real PCs, Windows-Linux, Buu with absorption mods, matches over 3 minutes.
- Xenoverse 2 parts (experimental, your copy): the male Majin CaC keeps its eyes and gets shorter legs; other proportions stay off.
- Infinite World (experimental): native ground grid in the minigames, Spaceship scoreboard I-IV, Bubbles' ready camera; no minigame is accepted as identical. Budokai 2 modes play music from your own disc; the five new scene transitions stay off.
- Linux (untested on Linux): links and "Open folder" buttons use `xdg-open`; the start log records distribution, kernel and session details.

- German and Italian on the US image: the game now uses `us/data_ger.afs` and `us/data_ita.afs` if you have them (it fell back to English before), checked in Edit Skills; the rest of the game in those languages was not reviewed.

Known issues: these stay disabled in this version: B2 scene transitions, board drawing, Bubbles machine and 7D2, Crusher Ball, rollback by default, XV2 proportions for human/Namek/Frieza, Piccolo Red Ribbon.

## v1.5.5.5 (2026-10-28)
- Infinite World (Dragon Mission): the story flows from the map to the video, the minigame, the result and back; Raditz and Bubbles' ground minigames can be completed (single yellow energy bar, Goku in frame, large rank letter); flights seen from behind with the camera ready at READY and the ring arrow under the feet; clouds and yellow fog back; Gohan's flight after Goten follows the original route and rings; original battle music when a mission starts.
- Dragon World (Budokai 2): all nine board phases can be completed; attacking a rival on the board starts the battle directly. Babidi's ship: enter and change floors, "Get skill!" window, battle trial with its VS card.
- Mr. Satan legend (beta): round 1 plays in full with subtitles, Satan's car arrives along the road with the original camera, less black screen between scenes; the legend moves through its rounds from start to finish.
- Hero mode (beta): 38 of the 52 gloves fit the idle, punch, hit and fall poses; the fighting-style list shows all 23 styles.
- Interface: shooting and button minigame result panels show rank and rewards without overlapping; Bubbles' panel shows "ITEM COLLECTION %".
- Free camera (beta) in the F1 menu. It starts off and is not saved.

## v1.5.5.4
- Mr. Satan legend: round introductions use Budokai 1's cameras (fighters whole, no black bars or cut-off edges), poses closer to the original (Trunks and Vegeta in armour, Ten Shin Han shirtless), no Next/Skip prompt in the shots.
- Infinite World: minigame cameras back to the original (buttons, Kienzan, beams, Ten's shot against Cell, field and flight).
- Tag Team (beta): the computer-controlled partner swaps in when its fighter is low on health.
- Dragon World (Budokai 2) and Babidi's ship: the game's background music no longer plays under the Budokai 2 music.
- Online (beta): rematches start in sync as a new battle for both players.
- Stability: no more lock after the game closes while preparing characters; a mod with a badly written capsule no longer crashes; settings, profiles and progress saves survive a power cut.
- Note: the first launch after updating prepares the PS2 stages again; it takes a while.

## v1.5.5.3 (2026-10-24)
- Battle camera: fixed the camera looking at the ground at the start of a battle and going wrong on grabs; the game's original camera is back (bug in 1.5.5 to 1.5.5.2).
- HD texture pack: new V2 of SSGPrinceVegeta's pack in Nova's Mods (6.01 GB) next to V1 (2.90 GB; installing one moves the other aside). Fixed the in-battle crash with any HD pack and the drop to about 25 FPS.

## v1.5.5.2
- Hero mode (beta): saga 4 playable; sagas 2-4 take place on the Earth map; Budokai 3 hairstyles fitted to the head (Goku, Vegeta, Gohan (Cell), Kid Goku; Bardock removed); changing the moveset in the creator now applies in battle.
- Mr. Satan legend: fixed Satan and the ring floor invisible in the final Cell ring scene (bug in 1.5.5 and 1.5.5.1); Satan squints in that scene like the original.
- Online (beta): desync report saved in `informes_online`; clear message if the host does not answer; shorter texts when mods differ; pause and quit if the connection drops mid-fight.

## v1.5.5.1
- Online (beta): 4 visible steps (host or join, share the code, connecting, fight), connection status with colour and ping, X copies or pastes the room code, errors say what to do, B always goes back; on a desync the game saves a report in `informes_online`.
- Hero mode: 1.5.5 did not install the saga data (Earth stages and parts catalogue). Fixed.

## v1.5.5 (2026-10-24)
- Infinite World (experimental): all 96 missions playable in the USA and European versions.
- Budokai 2 (experimental): board starts without crashes and asks "Is this okay?" after choosing an ally; Babidi's ship: all four floors work and you can return after editing skills; the Red Ribbon base no longer crashes the game.
- Mr. Satan legend: round 3 introduction and per-rival music.
- Hero mode (beta): sagas 1-3 playable (4-8 in development), Androids and Piccolo sagas on Earth; Budokai 3 or Xenoverse 2 parts in the creator (XV2 experimental, only with your own copy); Budokai 3 hairstyles (beta).
- Online (beta): fights stay in sync in tests; automatic reconnect when mods do not match.
- 480p (640x480) internal resolution option for modest PCs; complete translations.
- Known: online untested in long matches between two homes; Linux not fully tested; some added characters still show no aura.
- Credits: 🇮🇹⚠ 𝐒𝐏𝐈𝐃𝐄𝐑 ⚠🇮🇹 (Infinite World decompilation), SSGPrinceVegeta (HD pack), @ArielPVB (character models).

## v1.5.1 (2026-10-18)
- Budokai 2 campaign (EXPERIMENTAL): red cursor arrow, life column, portraits, event sounds, music and Budokai 2 stages on every board tile; Saibamen sprout from the ground; Babidi's Ship (beta) screens and trials closer to the original.
- Infinite World campaign (EXPERIMENTAL): map borders, music per area, smoother animation blending; training and mission fixes.
- Hero mode (beta): change your hero's moveset inside the game.
- New languages: Portuguese (Brazil) and Japanese (launcher and menus).
- Nova's Mods: Janemba and the Ginyu Force with their own voices; character slots kept between versions.
- Stage select: the Infinite World minigame fields are no longer listed, so every new stage fits (Snowy Mountains was missing).
- Faster: character icons and portraits are cached, so updates no longer redraw them all on the first launch.
- Fixed: on some PCs Nova's Mods and the update check did not load.

## v1.5.0 (2026-10-17)
- Dragon HUB: rank plates, new backgrounds/hosts, fly-in fighters, save icon, BETA/EXPERIMENTAL labels.
- Tag Team (beta): teleport relays, COM vs COM included.
- Mods now work when playing from the ISO (one-time preparation cache).
- PS2 gateway: pick your Budokai 1 / Budokai 2 / Infinite World ISO from the launcher or in-game; recognised by content.
- Renewed launcher (tooltips, Nova's Mods cards, ON/OFF state) and renewed F1 menu.
- New modes: Mission 100, Hero mode (beta, swappable movesets), Mr. Satan legend (beta, Budokai 3 models).
- Infinite World campaign and Budokai 2 campaign + Babidi's Ship (EXPERIMENTAL).
- Single transformation capsule, EU version (from the unreleased 1.4.6).
- Full translations (EN/ES/FR/DE/IT), including Nova's Mods descriptions.
- Fixes: ISO cache leftovers on failed copy, PS2 ISO choice save check, B2 board reload, safer HUB menu navigation.
- Credits: 🇮🇹⚠ 𝐒𝐏𝐈𝐃𝐄𝐑 ⚠🇮🇹 (Infinite World decompilation). Support: https://ko-fi.com/novapowers

## v1.4.5.1 (2026-10-14)
- Fixed: an HD texture pack installed from the launcher (Nova's Mods, zip or enabling it) was not used until the game was restarted. The pack index is now rebuilt when the list of packs changes, so the pack shows as soon as you press PLAY.

## v1.4.5 (2026-10-14)
- HD texture packs (Burst Limit style): images decoded on background threads with alpha-weighted mips already laid out for the GPU; the HD texture is created only once its image is ready (the original is shown meanwhile), single-block upload (row-by-row copies to the upload heap ran at 15 MB/s), pooled upload buffers, 64 MB of new HD textures per frame, texture cache limits from VRAM, RAM budget from free memory (no more paging freezes), learned preload of the textures used in previous sessions, cached pack index (startup 21-63 s -> under 1 s). Worst hitch with a 2.8 GB pack at 3x: 1.1 s -> 0.11 s, including toggling the pack live.
- Fixed: 1.4.4.5 loaded no texture pack (long-path prefix); x8+ packs; zip installs of big packs; first-run freeze while indexing.
- Nova's Mods: big mods downloaded in parts, author credit with links, search by author, never offers a downgrade. SSGPrinceVegeta's HD texture pack available.
- Launcher: transactional zip installs (the previous mod goes to mods_respaldo/), nested mods and multi-mod zips placed automatically, UTF-8 paths (accented/Cyrillic/Japanese names), crash-aware session memory that offers to disable the newest mods, mod conflict line, extra costumes no longer take a slot, clearer roster-build states with "play with the previous characters", search boxes, full IT/DE/FR text.
- Input: XInput falls back to SDL when no Xbox controller is connected.
- Modding Kit: `importar.py rehacer` rebuilds a port from its stored recipe; backups before rewriting a character; BOM-tolerant TOML; clearer warnings.

## v1.4.3.1 "1.4.3 EX" (2026-10-10)
- Performance: the host frame cap no longer sleeps the GPU thread at 60 (fixed "stuck at 30 FPS" on 60 Hz screens), evenly spaced guest vblanks, launcher capped at 60, process priority above normal, shipped shader/pipeline cache, AFS mod lookups cached and readahead with mods, Vulkan FIFO relaxed.
- Compatibility: high-performance GPU on hybrid laptops (D3D12 and Vulkan, Optimus/PowerXpress exports in the exe), AMD RDNA "device lost" fix (MaxAs clamp, from Xenia Canary), Intel Arc on the RTV path, automatic Vulkan fallback when Direct3D 12 is unavailable, better auto preset (AMD APUs, 4 GB cards), FidelityFX DLL delay-loaded, bilingual GPU error messages.
- HD texture packs: replacements re-checked by content (no wrong texture when memory is reused); Linux HD brightness; FR/DE/IT text.

## v1.4.2.3 "1.4.2 EX" (2026-10-07)
- Launcher Mods tab: "Easy textures" (capture while playing, prepare pack-named PNGs with the bundled Python, "My texture pack"), drag-and-drop install of mod zips and edited PNGs, plain-language broken-mod warnings (empty, nested folder with a Fix button, missing files, invalid texture names/sizes) and a warning next to PLAY.
- Modding Kit ships a portable Python (python.org build with numpy, Pillow, scipy, tkinter); the launcher and DBZ3_ModKit.bat use it first.
- Default texture capture folder is next to the game (was a developer path).
- Shin Budokai ports: empty donor yell slots are enabled (SQ flag + volume), specials live in codes 0x240-0x27F, own ball ASTs use low codes with both #AME links, recoil hold, full recolor of ball effects, SB sub-block header fix, donor ki blasts grafted (holding E no longer crashes).

## v1.4.2.2 (2026-10-07)
- New characters now work with the European (EU/PAL) executable too (region-aware guest addresses and hooks; roster_build reads eu/ and data_eng).
- The Kit ships Roboto Bold (Apache 2.0) as its font fallback.

## v1.4.2.1 (2026-10-07)
- New characters not showing: the rebuild crashed with "OSError: cannot open resource" when Arial Rounded (an Office font) was missing; fonts now fall back.
- The New characters tab warns when playing from the ISO (mods are not loaded) or with the EU version.

## v1.4.2 (2026-10-06)
- NVIDIA DLSS (beta, RTX + D3D12) and AMD FSR 3 (beta) with real depth, jitter, motion vectors and reactive mask; live switching; FSR 3 fallback.
- Shadows fixed with temporal upscalers; "More FPS" presets no longer drop below their promised scale.
- HD rim light ("HD shine") on characters is adjustable (Native mods tab, F1).
- Mod Kit: Shin Budokai 1 / Another Road importer (sbport, sb_tecnicas, sb_voces), Super Dragon Ball Heroes models (sdbh_model), camera Studio with Blender round trip, per-form models, per-capsule ki and donor physics in roster_build.

---

## [1.4.1] - 2026-10-05

Performance and polish.

### Fixed

- **Fixes for the 30 FPS lock-up on powerful PCs (issue #8)** (not reproducible on the dev PC; the new telemetry below names the cause on affected machines). The runtime now requests a
  1 ms system timer, uses high-resolution waitable timers for its short sleeps
  (guest vblank, GPU register waits, the game's own `Sleep` calls) and opts the
  process out of Windows power throttling (EcoQoS / timer-resolution
  throttling), so Windows 11 neither stretches 1 ms waits to ~15 ms nor parks
  the game on efficiency cores.
- The low-FPS warning no longer suggests "lower the internal scale to 1x" when
  it is already 1x: it lists only the expensive settings that are active, and
  says when the bottleneck is the CPU.

- **Crash on the character select screen with the new characters pack.** A new
  cell borrows its host's saved costume and costume count (Janemba on Krillin), so
  the game asked for costume 1-3 of a 1-costume character and read past its model
  list (null pointer in `sub_8208DDF0`, called from `0x82134A98`). Model/mouth lists
  are now padded to 8 costumes (repeating the first one) and costume cycling on a
  new cell only walks that character's own costumes.
- **Music silent with a music pack + the characters mod.** The virtual AFS table was
  built from the original `adx_usa.afs` while the data came from the music pack's
  whole-file replacement; it now uses the file actually opened.
- The launcher no longer runs `roster_build.py` on PLAY when the modding kit is not
  installed (the characters pack ships a prebuilt `_roster`).

### Changed

- **DRED (GPU crash diagnostics) is off by default** (it was always on since
  1.3.0, with a cost on every command list and resource). The game turns it on
  automatically for the session after a `D3D12 device removed`.
- The `perf` log line adds `gpu_wait=` (ms per frame blocked on the GPU),
  `syncs=` and `cp_wait=` (ms per frame the GPU thread waited for the game).
- The `entorno` line (component versions, OS, RAM) is also logged when the
  launcher is skipped.
- `dbz3: tiron N ms (...)` lines for guest frames over 50 ms (D3D12 and Vulkan),
  `perf` lines on Vulkan, and `dbz3: audio pico=... rms=...` (guest mix level) with
  `dbz3_perf_logging`.
- Launcher: the New characters tab warns when the game is the EU/PAL version
  (new characters need the US/NA one).

### Modding Kit 1.4.1

- New **Diagnostics** page (`diagnostico.py`, also usable from the console):
  reads a game log and explains, in plain language, old/mixed installs, 30 FPS
  lock-ups (CPU vs GPU), GPU resets, sudden closes, slow disks and new
  characters on the EU version; copies the report for Discord.
- Checks that the new characters mod is built for all five game languages.
- `make_modpacks.py --solo kit`: the characters pack stays the 1.4.0 one on
  Google Drive.

---

## [1.4.0] - 2026-10-04

The biggest update so far: an in-game quick menu, live video settings, a
redesigned launcher that opens instantly and works with a controller, and a
full system for **new characters** in their own select-screen cells.

### Added

- **In-game quick menu** — press **F1** or **Back + Start** on the controller
  during a match. Three pages (Picture, Sound & pad, Display) plus "All
  settings" (F4). Changes apply instantly and are saved automatically. The
  button combo can be changed to **L3 + R3** or keyboard only. While the menu
  is open the game does not receive your inputs.
- **"More FPS with FSR"** (Native / Quality / Balanced / Performance / Ultra
  performance) — the game renders below your resolution and AMD FSR upscales
  it, for real FPS gains. Available in the quick menu and in the launcher's
  scaling tab.
- **New FPS panel (F3)** — game FPS (frames the game actually produces) and
  display FPS, with a frame-time graph and a selectable screen corner. It
  replaces the old FPS window; the launcher's "Show FPS" option drives it.
- **Controller support in the launcher** — navigate with the pad, **LB / RB**
  (or **Ctrl + Tab**) to switch tabs, **START** to play. A hint bar next to the
  buttons shows keyboard keys or Xbox / PlayStation / Switch buttons depending
  on what you used last.
- **New characters in their own cells** of the character-select wheel, without
  replacing anyone: own icon, name banner, portraits, health-bar face, model
  preview framed to their height, techniques and capsules. Your save format does
  not change.
- **Character pack** (`DBZ3HD-1.4.0-Personajes.zip`, optional download):
  **Janemba, Android 19, Zarbon (with his Monster form), Dodoria, Guldo, Jeice
  and Burter**. Zarbon and Dodoria use their **Budokai 1 moves, combos and
  yells**.
- **"New characters" launcher tab** — list of installed characters (tick to
  enable / untick to undo), a slot table (free / taken / conflicts), a
  per-character editor with **live previews** (icon, name banner, P1/P2
  portraits, health-bar face) and "Create character" from your own files. The
  combined mod is rebuilt automatically when you press PLAY.
- **Character importer** ("Import a character from another game"): pick the game,
  search the character, check the name (live preview of the banner) and import.
  Sources: **Budokai 1** and **Budokai 2** (from your own ISOs), **Infinite
  World** (including community IW→B3 moveset ports) and **community Budokai 3
  models** (`.amb`, `.amo` + `.amt`). Shin Budokai 1/2 and Super Dragon Ball
  Heroes are detected but marked "in development".
- **Modding kit** (`DBZ3HD-1.4.0-Kit-Modding.zip`, optional download): the
  Python tools behind the modding tabs, the reference lists the importer uses,
  guides and a one-click requirements installer (`instalar_requisitos.bat`).
- **Own yells and voices** for new characters (a custom audio path, so no XMA
  encoder is needed), **custom capsules** per character (also importable from
  B1/B2/IW), working **Edit Skills** and inventory for new characters, and
  **extra costumes** for existing characters (`traje.toml`).
- **Model gateway** for modders (`awo_tools/b3_gateway.py`): PS2 models, OBJ and
  glTF 2.0 into the HD format, and export to OBJ for editing in Blender.
- **"Save at 100%" native mod** is now functional: it installs a complete
  Xbox 360 save that you provide (`mods_nativos/partida_100`), backing up your
  current save first, with a restore option.

### Changed

- **Launcher redesign** — new fonts and icons, cards, label/control rows, info
  markers instead of hover-only tooltips, a big orange PLAY button. Language
  moved to the right column of the Video tab.
- **Launcher startup from ~31 s to under 1 s** — the game folder is no longer
  scanned in full at startup; folders are listed on demand. (The SDK option
  `vfs_eager_scan = true` restores the old behaviour if ever needed.)
- **Video settings apply live** — internal resolution, upscaler (Basic / AMD
  CAS / AMD FSR), sharpness and FXAA change without restarting the game.
- **F4 over the game** now blocks controller input to the game while it is open
  and applies the changes when you close it.
- The old FSR quality setting is replaced by "More FPS with FSR" (default:
  Native, i.e. the same image as before).
- **Height correction for ported characters** — models ported from other games
  floated or sank; their hip height is now rescaled to the new skeleton.
- New runtime DLLs (`rexruntime.dll` 11,034,624 B, `rexgpu-xenos.dll`
  6,372,864 B, build stamp 1.4.0). Do not mix them with older versions.

### Fixed

- **Crash when using the quick menu** — the controller was read from two
  threads at once without a lock (heap corruption). All device access is now
  locked.
- **Launcher stuck on "working…"** forever when a modding tool printed 4 KB or
  more of output (one-byte buffer overflow in the tool runner).
- **Black screen** with the FSR2 / FSR3 present effects (runtime fix).
- The runtime configuration file is now always saved as valid TOML.
- Zarbon and Dodoria no longer show the donor's transformation effect or speak
  the donor's phrases, and their Dragon Rush keeps its final burst.

### Known issues

- New characters are **experimental**: some Budokai 1 grab techniques play as a
  normal hit and some effects are not perfect.
- New characters and mods need the game data as an **extracted folder** (not ISO
  mode), and new characters need the **USA** executable and data.
- The importer and character creator need the **Modding kit** and Python 3.11+.
- The new launcher and quick-menu texts are in Spanish and English; Italian,
  German and French show them in English for now.
- The Vulkan backend still works but is much slower than D3D12 (default).

### Credits for this version

The quick menu, FPS panel, live video settings, FSR below native resolution, the
FSR2/3 black-screen fix and the TOML fix are adapted from the `burstlimit`
branch of [iExplosiveRage/rexglue-sdk](https://github.com/iExplosiveRage/rexglue-sdk)
(the SDK of **DBZ Burst Limit Recompiled**), restyled for this port. Thanks to
the Budokai modding community for the models and Infinite World ports used in
the character pack.

### Resumen en español

- **Menú rápido en partida** (F1 o Back + Start; también L3 + R3 o solo
  teclado): imagen, sonido y mando, pantalla y «Todos los ajustes». Se aplica al
  momento y se guarda solo.
- **«Más FPS con FSR»**: dibuja por debajo de la resolución y FSR reescala; FPS
  reales en equipos modestos.
- **Panel de FPS nuevo (F3)** con FPS del juego, FPS de pantalla y gráfica.
- **Ajustes de vídeo en vivo**: resolución interna, escalado, nitidez y FXAA sin
  reiniciar.
- **Launcher rediseñado**, abre en **menos de 1 s** (antes ~31 s) y se maneja
  **con el mando** (LB/RB o Ctrl+Tab cambian de pestaña, START = Jugar; ayudas
  con botones de Xbox, PlayStation o Switch).
- **Personajes nuevos** en casillas propias del select, sin sustituir a nadie.
  **Pack de personajes** (descarga opcional): Janemba, Androide 19, Zarbon, Dodoria,
  Guldo, Jeice y Burter; Zarbon y Dodoria con golpes y gritos de Budokai 1.
- **Importador de personajes** desde Budokai 1, Budokai 2, Infinite World y
  modelos de la comunidad, con vista previa del nombre en directo. **Kit de
  modding** (descarga opcional) con las herramientas y un instalador de
  requisitos en un clic.
- Gritos y voces propios, cápsulas propias, Edit Skills para los nuevos y trajes
  extra para personajes existentes.
- **Arreglos**: cierre al usar el menú rápido, launcher colgado en «trabajando…»,
  pantalla negra con FSR2/FSR3, personajes portados que flotaban.
- **Avisos**: los personajes nuevos son experimentales, requieren la versión USA
  y los datos en carpeta (no ISO); los textos nuevos están en español e inglés.
- Mejoras del SDK adaptadas de **DBZ Burst Limit Recompiled** (iExplosiveRage).

---

## [1.3.0] - 2026-09-30

- One-click **Repair installation**, controller button labels (Xbox /
  PlayStation / Switch) in the Controls tab, **DRED** on by default for clearer
  GPU crash reports, `gamecontrollerdb.txt` shipped next to the executable,
  texture-extractor fix (issue #13), launcher polish and caching.

## Older versions

| Version | Date | Summary |
|---|---|---|
| v1.2.9 | 2026-09-26 | Self-explaining diagnostics: sustained-FPS, slow-disk and mixed-install warnings; VRAM guard |
| v1.2.8.2 | 2026-09-24 | Texture enhancement no longer tanks FPS (dynamic-texture throttle) |
| v1.2.8.1 | 2026-09-23 | Texture dump covers uncompressed HUD/UI; RGBA8 packs (issue #11) |
| v1.2.8 | 2026-09-21 | Texture dump fix (chosen folder reaches the plugin) |
| v1.2.7 | 2026-09-21 | PCSX2-style texture packs (D3D12 and Vulkan) |
| v1.2.6 | 2026-09-20 | Polished HD texture enhancement + self-repairing settings file |
| v1.2.5 | 2026-09-19 | Focus QoL (mute/dim), disk diagnostics and read-ahead |
| v1.2.4 / EX | 2026-09-19 | Real volume, update check, FXAA/dither, GPU knobs, portable user data |
| v1.2.3 | 2026-09-18 | Performance counter and quieter AFS log |
| v1.2.2 EX | 2026-09-17 | Launcher finds the executable by itself + ISO-mode fixes |
| v1.2.0 / 1.2.1 | 2026-09-14 | Renewed mod center, polished HD↔HD model swap, FSR/CAS sharpness |
| v1.1.x | 2026-09 | USA+EU dual core, single universal exe (SSSE3), disc (ISO) mode, i18n |
| ≤ v1.0.x | 2026-08 | Initial stabilization |
