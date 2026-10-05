# Ape Escape "Power Up Gadgets" — MODLOG

Journal per the mod-any-game loop. Plan/recon: `MODDING_PLAN.md`.

## Idea / done means
Press an equipped gadget's button again to power it up (glow ring on HUD icon + pitched-up switch SFX), again
to power down. One gadget at a time, tuned visually by the user. Done = each gadget works in game, then a
20-45 s showcase clip and a publish-check'd release (source + manifest only, no disc data).

## Route
Native plugin built from source (PSXRecomp v0.3.0, ApeEscapeRecomp repo) + recompiler mod_function_entry_funcs
hooks. Reason: new projectiles, models and flight physics need real code; declarative patches can't do it.

## Paths
- Official install (untouched): D:\Ape Escape Recomp\ApeEscapeRecomp-v0.3.0-windows-x64
- Disc: D:\Ape Escape (USA)\*.cue (Redump-verified, user-owned)
- Source: D:\Ape Escape Recomp\modding\ApeEscapeRecomp -> build\ApeEscapeRecomp.exe
- Plugin: src\mods\ape_power_up_gadgets_plugin.c; manifest: mods\preloaded\packages\ape.enhancement.power-up-gadgets
- Build: modding\vsbuild.cmd (MSVC, -DPSX_DEBUG_TOOLS=ON, C flags "/DWIN32 /D_WINDOWS /experimental:c11atomics").
  New hooks -> game.toml [recompiler] mod_function_entry_funcs, then `psxrecomp_cli.py generate`.
  Hooks: 0x8009A800, 0x8009ABF4, 0x80064208, 0x80016A60, 0x80016B94, 0x80063628, 0x8009B1C0, 0x80028CC0.
- Test loop: modding\cycle.ps1 (kill by path, rebuild, PSX_NO_LAUNCHER, load debug savestate slot 9 = menu slot 10).
  LNK1104 = exe still locked, wait 2 s and rerun. Debug TCP 4480 via re\dbg.py; helpers in re\.

## Backups / restore
- Saves: `um backup create ".../build/saves" --name ape-escape-saves` (snapshot 2026-10-01 14:12).
  Restore: `um backup restore ape-escape-saves`. Older manual copy: build\saves\card1.mcd.bak-20260930-160602.
- Official install never modified.

## Status
- Boomerang Stun Club: done.
- Grapple Net: done. 2026-10-01: grabber (coil, rope, claw) was a no-depth overlay that always showed in front
  of Spike; now depth-sorted into the game's ordering table (OT = 0x800EC3D0 + 0x4078*[0x800EC35C], packet ptr
  0x800EC354, otz = ZSF3(341)*3z>>12, clamp 0x1B..0xFBF), linked at DrawOTag hook 0x80028CC0 (the table gets
  rewritten during the net swing after Spike draws). Far depth compressed 1/4 past hand+24 so the thrown claw
  isn't buried by ground polys. Test: re\depthtest.py (4 facings), re\grabtest.py <name> 70 (sideways throw;
  straight-ahead throws hide the claw behind Spike from the default camera, which is correct).
- Sky Flyer: AE2 handle-under-propeller model done; AE2-style flight (rise -> hang 12f -> slow descent, 1.4x
  horizontal) in, awaiting user tuning.
- SMG (Slingback Shooter, gadget 3, 2026-10-01): handler 0x8009AD60 (gadget switch table 0x800A5B28), model
  entry 0x800EC090 drawn via 0x800E9E40; band from model verts 1/5, pouch = player mesh vert 325 x matrix
  0x800F4930. Pull states 0x80-0x83, release 0x84 (anim event -> fire() 0x8007AA98 -> 0x80085630 -> spawner
  0x80085254, pellet pool 0x800BF300 4x0x1C). SMG model on disc user offset 0xE8920 (0x2DC, untextured; found
  in level with SMG monkey = debug slot 11 / menu 12). Bullets: ours, club attack sphere per game frame.
  Aim is stock (pull stick back, shoots opposite). Test: re\smgtest.py; NB debug stick must be fully
  deflected (ry=1 doesn't register).
- GPU rule (2026-10-01): never write GP0 from VBlank and never allocate from the game's packet buffer at DrawOTag
  (both desynced the GPU: garbled colours, freezes, "GP0 unknown command" in psx_last_run_report.json). All our
  drawing = packets in our own psx_mod_alloc_gpu_dma_memory arena per buffer, linked at DrawOTag 0x80028CC0.
  Check: debug cmd gpu_opcodes (no 0x03-0x1F / 0xE7+), gpu_frame_dump frame=N for context.
- Runtime gotcha: psx_mod_register_function_entry_plugin keeps ONE hook per (plugin id, address); a second
  silently fails. Entry hooks run whether the feature is enabled or not.
- HUD: the game's HUD list hangs off OT entry 0 via table+0x4000 (drawn after the main table); our glow is
  appended at the very end of that chain. Club blade = model 0x800EC0F0 (colours blue/teal/white), halo = glow
  beam 0x8002020C (a3 colour, ra 0x8009AD34); both red while powered.
- Git: branch `power-up-gadgets` in ApeEscapeRecomp (first commit 1abe4da); prepared_disc/ + psxrecomp/ are
  excluded via .git/info/exclude (disc data, never commit). Commit each working gadget step.
- Owed: remove debug-only code (PUGD block 0x9F000000, object logger, 0x80016B94 + 0x800161C0 hooks, part masks);
  showcase clip; publish check.

## Key facts
Full address list and model format notes: Claude memory `project_ape_escape_mod.md` (mirror important ones here
as they change).
- Held gadget 0x800EC2D2, slots 0x800F51A8, Spike actor 0x800EC1E0, set_state 0x80064208, move step 0x80063628.
- Sky Flyer handler 0x8009B1C0, prop obj 0x800EC0C0, disc obj 0x800EC180.
- Collision list 0x800B20D8 (3 monkey, 4 enemy, 6 club attack, 8 net capture).

# Mod 2: Play as Jake (ape.enhancement.play-as-jake)

## Idea
Play the whole game as Jake (Spike's blue/purple-haired friend); wherever Jake appears (his race), he wears
Spike. Look only: skeleton, animation, controls, AI unchanged.

## How (2026-10-01, working in normal level + Jake race)
- Plugin src\mods\ape_play_as_jake_plugin.c, feature play-as-jake (enabled in build\mods\state.toml).
- Character object: words +0 hdr (u16 +2 polys, +4 parts, +6 verts, +8 colours), +4 colours, +8 polys,
  +0xC part table (12 B: nverts, first, bone offset xyz, parent+1), +0x10 verts, +0x14 bone matrices
  (part i -> matrix i+1, 0x20 each, posed by 0x80015504 before transform 0x80016A60).
- Spike 16 parts; Jake 18 (same 0-15; 16/17 = hair chained off head part 3). Jake object in race 0x800E5500.
- Jake mesh on disc: user-data offset 0x033E23EC (KKIIDDZZ.DAT, 0x1E38 bytes, part0 y offset 0 on disc vs
  -133 in RAM). Face TIM-like block: palette 0x033BDCB0, packed image +0x2C (0x1B8 bytes): nibble RLE,
  byte with equal nibbles = pixel x (next byte + 2), else two pixels low-first. Stock face page 9 (576,0),
  palette (880,216); retargeted to page 15 texels (984,128) palette (960,255) (zero in slots 8/9/10).
- Poly records: textured 0x24/0x2C/0x34/0x3C = 0x1C bytes (+12 uv0 clut, +16 uv1 tpage, +20 uv2, +22 uv3),
  untextured 0x20/0x28/0x30/0x38 = 0x10.
- Plugins need an activation/vblank callback to count as "trusted"; entry hooks run regardless of feature
  state, so they're gated on activation.
- Test saves: debug slot 9 (menu 10) normal level; debug slot 10 (menu 11) Jake race.
- Not done: HUD icons (L2 face, lives shirt, race map head) still Spike; cutscene check; VRAM strip only
  verified free in 3 levels.

## 2026-10-01 SMG aim + gunfire
- Aim flipped: the game keeps a 40-entry stick ring at 0x800E5320 (4 s8 per sample: rx, ry, lx, ly; newest
  index u32 at +0x170), turned into aim by pad update 0x8001325C (new hook). While the powered SMG is held,
  each new sample's right stick is negated at that entry -> shoots where the stick points.
  (Raw pad 1 buffer 0x800E52D0: +4 rx, +5 ry, +6 lx, +7 ly.)
- Gunfire SFX: slingshot sounds captured by logging SPU voice regs from the plugin (debug read_ram can't read
  SPU): aim start 0x03B90, release snap 0x049A0 (pitch 0x3AB, vol 0x2AD4), flight 0x04F50, hit 0x06D00.
  SMG plays 0x049A0 at pitch 0x600, vol 0x2200, retriggered on our held voice each shot (ape_play_sfx).
- Ammo lock: slingshot ammo type at 0x800F51C0 (0 standard, 1 explosive, 2 homing; cycled by R3 or by
  pressing the slingshot button again). While the SMG is powered: R3 masked out of pad 1 (0x800E52D2 bit 2,
  active low) at the pad-update hook, ammo held at 0; the pre-power-on choice is restored for 20 frames
  after power-down so that press's own cycle doesn't stick. Test: re/ammotest.py.

## 2026-10-01 Jake: HUD icons + ponytail sway
- HUD icons recoloured/swapped at DrawOTag by walking the HUD chain (table+0x4000) for sprites (E1 page word
  then 0x64..0x67 sprite) matching page+uv+palette, and pointing them at our palettes uploaded with the face:
  L2 face page 0x17 uv 8060 pal 0x7E1D -> purple hair, clut (976,255); lives shirt (pause menu) page 0x4F uv
  60C0 pal 0x3AFF -> navy, clut (992,255); race map heads page 0x26 pal 0x3F9B, Spike uv F860 / Jake uv F868:
  swapped, Jake's made purple, clut (976,254). Stock palettes untouched. Tools: re/hudwalk.py, hudicon.py,
  otsprites.py.
- Ponytail: where Jake isn't in the level the tail swings on a spring (head-local tip direction, pulled to rest,
  pushed by Spike's world velocity/acceleration and carried against head turns; head-to-world = camera
  0x800E78C0 inverse x head matrix, columns renormalised for the screen y scale). Race still copies Jake's live
  tail. Test: re/tailtest.py.
- Still open: in-engine cutscenes unchecked.

## 2026-10-01 Jake: double-jump voice
- Spike's double-jump yell = SPU sample 0x38F00 (0xC30 bytes, keyed pitch 0x4B8; disc user offset 0x159EF0).
  Jake's jump yell (race, water jump ~4.7 s in) = SPU 0x71030 in the race bank (0xDE0 bytes, pitch 0x5D2;
  disc user offset 0x2E9948, also 0x317DF8), keyed with the generic jump whoosh 0x03530.
- Plugin reads Jake's from disc, decodes, resamples 0x5D2 -> 0x4B8 rate, re-encodes ADPCM (best of 5 filters x 13
  shifts per block; 24 dB vs source) to 0xB50 bytes, and every 30 vblanks writes it over 0x38F00 via the SPU
  manual transfer port (0x1F801DA6 addr, 0x1F801DA8 FIFO; restores DA6) when that slot still holds Spike's yell
  (compared against the disc). Reinstalls after level loads/savestates (verified in the race).
- Debug: spu_events (KEYON ring with addr/pitch), spu_ram (peek; returns UPPERCASE hex). Tools: re/keyons.py,
  spuwav.py (ADPCM -> wav), djump.py, racecorr.py. Extracted wavs in modding/sfx (not committed: game audio).
- Stun Club swing yell = SPU 0x39B40 (0x8D0 bytes, pitch 0x4B8, disc 0x15AB30; keyed with swing whoosh 0x070A0).
  Voice code now takes a list of Spike yell slots; Jake's yell is cut to fit smaller slots with a 320-sample fade
  (swing slot keeps the first 0.30 s of 0.39 s; the dropped tail is the quiet end). Test: re/swingsfx.py.

## 2026-10-01 Jake: redrawn HUD icons
- Face icon redrawn (two hair spikes, round dome, no yellow headband) and lives shirt redrawn (sleeveless navy
  top, no stripes) as 4-bit art in the recoloured palettes' indices, uploaded over Spike's texels in place with
  the face texture: face page 0x17 -> VRAM (472,384) 5 halfwords x 16 (texture page bit 4 = +256 in y! a first
  try at y 128 overwrote unrelated texels), shirt page 0x4F -> (1008,96) 8 x 32. Arena raised to 0x2000.
- Designs: modding/art/face_new.txt, shirt_new.txt (face_design.py / shirt_design.py preview, art.inc -> C).
  Note for publishing: the face's lower rows (eyes/mouth) are copied from the game's Spike icon.
- Silenced Spike lines (silent block, flags 0x05, voice ends at once - END_STOP same frame): club spin 0x38560
  (also plays on a catch), "Gotcha!" 0x36B30, hurt cry 0x3A420 (hurt state 0x1B). Spike voice bank is
  contiguous: 0x36B30, 0x38560, 0x38F00, 0x39B40, 0x3A420...; disc offset = SPU addr + 0x120FF0. Catch also
  plays 0x3F0E0 + jingle 0x2B230/0x18990 a second later (kept). Found with re/recorder.py while the user
  played (logs non-music key-ons with Spike's state), spinsfx.py, stockcatch.py, netswing.py.
- Level-clear outro "Yes!": not an SPU sample - the outro music is CD-XA (file 1, channel 6; ch 7 identical;
  interleave from LBA 40187, ch6 sectors 40193 + 8k, 145 sectors, 7.73 s stereo 37.8 kHz) and the voice is
  baked in: first ~0.62 s, pure mono, f0 ~390 Hz, music (stereo) starts ~0.6 s. No voiceless copy on the disc
  (channels 0-5 are other 9.28 s streams). Fix: plugin vblank watches cdrom_debug_get_sector_history; when the
  interleave's first sectors (40187..40192) are read it holds SPU CD input volume (0x1F801DB0/DB2) at 0 until
  15 ch6 sectors are delivered, then fades back (6 frames) to the game's value; 120-frame cap. Measured: mute
  40 ms before first CD audio, unmute after 0.73 s. Tools: re/xadec.py (XA -> wav from the .bin),
  outrocd.py (sector log), outromute.py (spu_status poll), outrocatch.py (catch on save slot 1 = menu 2).
- CRASH FIX (savestate loads froze the game): the game runs with the SPU IRQ enabled (SPUCNT 0xC0C1, bit 6)
  and IRQ address 0; setting the transfer address / FIFO writes are IRQ compares in the runtime, and restoring
  DA6 to 0 after installing Jake's yell raised an unexpected SPU IRQ -> wedged in the exception handler
  (freeze dump: in_exc=1, i_stat 0x200). Fix: don't restore DA6 (the game sets its own before each transfer)
  and skip a slot the IRQ address points into. Stress test re/sstress.py: 20 loads of slots 9/10/1, 0 IRQ
  events, game alive. Rule: never write SPU DA6/DA8 at an address matching DA4 while SPUCNT bit 6 is set.
- MODEL GLITCH FIX: the mesh swap (transform entry -> DrawOTag) covers most of a frame, so savestates usually
  capture Spike holding Jake's mesh words (mod memory 0x9F000400..). Loading one left them stuck (part count 18
  -> transform hook bailed -> tail bones never posed), worst when loaded right after boot (Spike's real words
  never seen). Fix: jake_unstick at transform entry puts the other object's own words back first; Spike's
  model is resident at the same place in every level (0x801030BC, 0x801030C8, 0x80104748, 0x80103208,
  0x801032C8 - checked in 5 saves), used as fallback after verifying 16 parts. Race Jake's own words are
  remembered each frame for the same reason. Debug server can't create savestates ("LLE run"), only load.

## 2026-10-02 Jake: extra-life pickup is Jake's shirt
- The 1-up is level model entry 0x800F4D40 (same entry in all 5 saves checked: header 40 polys, 20 verts,
  13 colours; polys 28 bytes each - tri and quad alike - palette u16 at +14; stock palette 0x36FF = (1008,219),
  texture page 0x2F, a red patch with a white stripe at u 96..127, v 64..95). Found it from the GPU frame dump
  (re/primsat.py: polys in a screen box) + write trace on the packet buffer filtered by palette (writer
  0x80019D48, a1 = poly pointer). Zeroing verts of other table entries didn't hide it (re/lifefind.py).
- Patch (in place in level data, idempotent, rescans the table 0x800F4000..0x800F5000 every 30 frames when
  the cached entry stops matching; identified by counts + verts 0/18/19): every poly's palette -> navy
  palette uploaded at (960,254) with the other Jake palettes (whites mapped to navy too = no stripe);
  sleeves folded: 2->1, 14->13, 3->4, 15->16 and 11->0, 5->12, 10->9, 17->8 (re/lifemesh.py dumps mesh/texture).

## 2026-10-02 Jake: Spike's warp-in "I'm going for it!" muted
- It's CD-XA, not SPU: file 1, channel 1 of the voice interleave starting LBA 51083 (16 channels; ch1 sectors
  51084 + 16n, 11 sectors to 51244 which has the EOF flag; coding 0x05 = 18.9 kHz, mono in stereo, 1.17 s).
  Found by logging cdrom_sector_history on the user's warp-in save (menu slot 3 = debug 2); SPU key-ons in the
  window had no Spike voice. Other channels of that interleave are other ~1.2-1.7 s clips (not identified).
- Fix (jake_spawn_vblank, same idea as the outro): CD input volume held at 0 from the first delivered ch1
  sector in 51084..51244 until 30 frames after 51244, then 6-frame fade back; 150-frame cap. Level music is
  sequenced (SPU), so nothing else is lost. Measured (re/spawnmute.py): muted before CD push began, all
  51744 pushed frames (= 1.17 s) went out muted, volume restored 1.55 s after start.

## 2026-10-02 Jake: title screen
- The title screen's running Spike is another object (0x80137850, mesh loaded at 0x80154020, matrices in the
  shared buffer 0x800E9C60), drawn by the same transform 0x80016A60 (found with fn_filter/fn_entry_tail on it).
  Its mesh header bytes equal level Spike's (1C00 6801 1000 4801 5000 0100), so the transform hook now swaps
  Jake into ANY object with Spike's header, tail posed at rest (the sway physics track the level Spike only).
  An object stuck holding our copy (savestate) is still treated as Jake, not as race-Jake. Checked: title
  (debug slot 0), level (9) and race (10) all still right.

## 2026-10-02 Jake: "JAKE EDITION" on the title screen
- Art: modding/art/edition.py (logo-style renderer: per-letter tilt/bounce, yellow->orange fill with streaks,
  pale rim, navy outline, blue extrusion down-left) + edition_tex.py (Knewave, OFL, set upright with a -0.28
  shear; 256x62, 15 colours + transparent -> art/edition.inc, copied to src/mods/ape_jake_edition.inc).
  Fonts tried are in art/fonts (Google Fonts, OFL/Apache) - credit Knewave (Tyler Finck) when publishing.
- The logo is a 3D model drawn by the title overlay itself (not 0x801379A0/B0). Title = overlay code present:
  0x80136930 == 0x0C005870 (jal 0x800161C0) and 0x80136C94 == 0x0C04DB40.
- VRAM free on the title: x 960..1023, y 384..~495. Texture at (960,384) 4-bit (page 0x1F: y base 256, so
  v starts at 128!), palette (960,448); uploaded with the Jake uploads every 8 frames (arena 0x2000->0x4000,
  copy packets 7 rows = 227 words). Quad 0x2D raw at screen (208,138) 108x22 (1.2x wide for the 384-px
  picture), linked after OT entry 0 so it's on the scene, under the menu.
- BUG on the way: packet tag count 8 for a 9-word quad -> GPU ate the next command's word, whole title
  misparsed (font sheet where the logo was). Tag count = words after the tag, always.
- 2026-10-02 fix: the attract demo (after ~45 s idle on the menu) plays a level with the title overlay code
  still resident, so the code-word check alone showed the text over the demo. jake_on_title now also
  requires that the level Spike (0x800EC1E0, never drawn on the title) hasn't been transformed for 60 frames.
  Checked: text gone through the whole demo, back ~2 s after Start returns to the title.

## 2026-10-02 Jake: cutscenes (Spike and Jake trade places)
- In-engine cutscenes (e.g. the New Game intro) load their own copies of the cast, drawn by the same transform
  0x80016A60 (called from the cutscene overlay, ra 0x8013988C) into their own OTs (DrawOTag a0 0x800F03CC /
  0x800F4444, two per frame). Intro: Spike = obj 0x800BF97C with a Spike-mesh copy whose header differs only
  at +10 (00 vs the level mesh's 01); Jake = obj 0x800BF960 with an 18-part cutscene mesh (header
  22 00 27 01 12 00 0F 01 67 00 ..., passes jake_is_jake_mesh). Before: the Spike copy wasn't recognised and
  Jake got the level Spike's mesh -> two Spikes.
- Now: Spike header matched on its first 10 bytes; a non-level Spike seen within 4 DrawOTags of a Jake trades
  meshes with him (each gets the other's own words, so the cutscene's loaded textures are right), Spike gets
  Jake's live tail pose (jake_pose_tail_live). Without Jake around, the disc Jake mesh as before (title).
  Unstick covers both directions. Tools: re/introlog.py (New Game from title save, logs screenshots +
  transformed objects), re/press.py (pad taps).
- Note: cutscene Jake's hair is his stock blue (cutscene texture); in-game Jake (disc face + HUD) is purple.

## 2026-10-02 Jake: NORMAL / EVIL look, picked with L1 on the title
- EVIL = the race Jake (mesh 0x033E23EC, face clut 0x033BDCB0, red slanted eyes; purple hair on the HUD).
  NORMAL = Jake before Specter: the intro cutscene's mesh (disc user offset 0x008AE9D0, 0x1D8C bytes, header
  22 00 27 01 12 00 0F 01 67 00 00 00; only 3 textured quads - eyes/mouth - whose uvs are relative to the
  64x64 face block, so the race retarget (+96,+128, page 15, palette (960,255)) works unchanged) and the
  intro's base face block (container at 0x06625754: palette 0x06625764, image hdr 0x06625784 = 768,64 16x64,
  packed 0x06625790, 0x2C0 bytes; same nibble RLE). Found by dumping RAM/VRAM in the intro (re/cutjake.py),
  then disc search: the mesh is on the disc 5x (only the face quads differ = per-file texture placement);
  the cutscenes keep eye/mouth expression frames as separate packed pieces (16x48 eyes, 16x16 mouths) next
  to a base block per character; Jake's base block = piece at (768,64).
- Plugin: both looks loaded from the disc and built at the first Spike transform; jake_select_look copies the
  chosen one into g_jake_mesh / g_face_*; jake_is_our_mesh covers both. HUD palettes: NORMAL swaps r/g of
  purple colours (b > r > g+1) -> blue hair. Cutscenes: NORMAL = Spike/Jake trade (cutscene textures, animated
  face); EVIL = Spike gets the evil disc mesh (face from our VRAM slot, which the intro leaves alone), Jake
  still wears the scene's Spike.
- Title UI (art/title_atlas.py -> src/mods/ape_jake_title_atlas.inc, our own art): 256x110 4-bit atlas at
  (960,384) (palettes at (960/976/992, 494)): JAKE EDITION, NORMAL/EVIL labels (Luckiest Guy - Knewave's V
  reads as I small), "L1" hint, two 36x32 portraits (art/portrait.py: HUD icon doubled + hand-drawn eyes).
  4 quads linked after OT entry 0. L1 edge (pad 0x800E52D2 bit 10, active low) switches, saves
  mods/play-as-jake.cfg ("look=normal|evil"), forces an upload, plays the menu cursor sample (SPU 0x2BFC0,
  pitch 0x3E0 normal / 0x2C0 evil; stock 0x344) on a claimed free voice.
- BUG on the way: upload arena 0x4000 overflowed with the bigger atlas -> last copy packets silently dropped
  (bottom rows garbage). Arena now 0x6000.
- 2026-10-02 title declutter (user: "looks cluttered"): portraits and the L1 hint removed; only the look's
  word (NORMAL / EVIL, Luckiest Guy) beside JAKE EDITION, centred at x 348, 15 px tall. Atlas now 256x86
  (edition + both words), one palette. JAKE EDITION moved to x 201 to leave a gap. art/portrait.py unused.
- 2026-10-02: small "PRESS L1 / TO SWAP" (Luckiest Guy, cream + navy outline, no extrusion; atlas 176,62
  59x24) drawn 16 px tall above the NORMAL/EVIL word (y 124), same centre x 348.

## 2026-10-02 Jake: all warp-in voice lines muted
- User heard "Let's go!" landing in the first level after New Game. re/newgamelog.py (New Game from title
  save, logs XA clips + screenshots; Start skips cutscenes): at the first level, file 1 ch 4 of the same
  voice interleave (51083). Decoded all 16 channels: 0-3 = four warp-in lines (1.17/1.17/1.71/1.17 s),
  4-7 identical copies of 0-3, 8-11 different clips (1.71/1.39/1.71/2.45 s, not identified), 12-15 empty.
  Ch0/4 = "Let's go!" (first level), ch1/5 = "I'm going for it!" (warp-in save).
- jake_spawn_vblank now mutes any delivered sector of channels 0-7 in LBA 51083..51340 and ends on the
  sector's end-of-file submode bit (0x80) + 30 frames, instead of one hard-coded channel/LBA.
  Verified (re/newgamemute.py): ch0 played in the first level, CD volume 0 from the first push, all 51744
  pushed frames muted, restored 0.5 s after the last sector.
- 2026-10-02: user listened to the exported clips (modding/sfx/warpin_clips, not committed): ch8-11 are Spike
  too -> mute channels 0-11 of the 51083 interleave (LBA up to 51460, ch11 ends 51446), cap 240 frames
  (ch11 is 2.45 s). No other Jake voice lines to swap in (only his race yell). Where ch8-11 play: unknown.
  re/voicepitch.py (autocorrelation f0) couldn't separate speakers - all ~220-330 Hz kid voices.

## 2026-10-05 Jake: hurt cry = Jake's real "ouch" from the disc
- AI voice-design takes (fal Qwen3 voice design + Maya, ~2 cents, modding/sfx/jake_ai/ow_round1) didn't sound
  like Jake (user). User found Jake saying "ow, ouch" in the cutscene after the Jake Attack 3 race.
- Cutscene dialogue lives in the streamed region LBA 53859-183488: data sectors interleaved with XA audio, file 1
  ch 1, stereo 37.8 kHz (coding 0x01), every 4th sector, no EOF bits (re/xaindex.py indexes all XA streams;
  ffmpeg -f psxstr on the carved region decodes its audio, 1686 s). Located the user's YouTube clips by
  spectral-flux cross-correlation + loudness-envelope check: conversation scene at stream time 1236.70 s,
  post-race "ow, ouch" at 1165.31 s (clip t 5.3). Jake's speaking pitch ~210 Hz median (Spike ~266), brighter.
- "ouch" = stream sector index 21873 = LBA 143876, sample 0, 18522 samples @37.8k (user picked cut 02B).
- Plugin: g_yells entries now have a source (JAKE_YELL / JAKE_SILENT / JAKE_OUCH); jake_load_ouch decodes the XA
  sectors from the disc (4 warm-up sectors, stride 4 LBAs, checks file/chan/coding), mono mix, fades;
  jake_build_ouch box-filters it to the hurt slot rate (key-on pitch 0x4B8 confirmed via spu_events) at
  natural speed - a first version squeezed all 0.49 s in (10% faster) and the user said too high pitched;
  now the last ~40 ms of the "ch" is cut with a 30 ms fade (slot 0xD00 = 0.448 s). SPIKE_YELL_MAX_SIZE 0xD00.
- Verified: SPU 0x3A420 holds 0xD00 bytes, matches the approved clip at the same speed (corr 0.965); user
  confirmed in game. Extracted cutscene lines (game audio, not committed): modding/sfx/jake_cutscene,
  jake_ow_real.

## 2026-10-05 Jake: purple gadgets (Play as Jake mod)
- Club blade: model entry 0x800EC0F0 colour ptr (+4) -> our violet list (F80058/880030/F8A0B0 words) while it points
  at stock blue/teal/white. Halo: glow beam 0x8002020C ra 0x8009AD34, a3 stock 0x00FF8080 -> 0x00BE005A (deep).
  User: first purple read pink -> moved to violet (hue ~261).
- Net: transform 0x80016A60 with ra 0x8009932C, a0 = net object (0x800E07D0); colours obj+4 (count u16 hdr+8 = 25
  greyish blues) copied hue-shifted to 261 deg (lightness kept, sat x1.5 min 0.35) into mod memory, obj+4 repointed.
- Spin trail: 0x8001F954 builds up to 12 quads (0x28 each: tag len 9, E1000220, 4x colour+vertex, code 0x3A) from
  packet ptr 0x800EC354; colour = fade<<16 in code (blue only). Hook entry -> note start; at DrawOTag recolour
  matching quads in place (red = fade/2). Found via gpu_frame_dump + wtrace_range on the packet address.
- Gadgets compat: powered red blade check now accepts a colour list in mod memory (0x9F000000+, where mod allocs live;
  first version only allowed game RAM so the powered blade stayed purple). purple -> red -> purple verified.
- "Blade longer after spin then shrinks": stock. Blade model scale is constant 1.0 (draw matrix 0x8009ABF4 a0, after
  undoing the 0.74 aspect row); the glow beam is a streak from the tip to a lagging point (a0/a1 at 0x8002020C).
- Hooks: Jake branch 0x80016A60, 0x80028CC0, 0x8002020C, 0x8001F954; all-mods 13 (gadgets 12 + 0x8001F954).

## 2026-10-05 Jake: Super Hoop + matching icons
- Hoop: model entry 0x800EC0A0 (32 verts; colours cyan F8F800 + greys) tints a 4-bit stripe texture (tpage 0x3E,
  palette VRAM 944,503: white/yellow/dark). Cyan tint can't make purple -> tint neutral 0x808080 + palette
  blue FFE0 / violet FC0B / edge FE60.
- Gadget ring icons = 0x65 sprites in the main OT, one palette each at x 944: net y448, hoop y451, club y456,
  flyer y458 (Select menu uses the same). Net mesh (see-through bag) texture palette 944,508. All recoloured to
  hue 265 (lightness kept; only blues on club/net, greens on hoop). JAKE_CLUT_SWAPS table, uploaded with the face
  packets only while gpu_vram_peek shows the stock palette.
- Menu net model = gadget table entry 0x800EC070 (same layout as the hand object) -> same violet copy.
- Tools: re/hudsprites.py (HUD list sprites), re/ringprims.py (textured prims in a screen box).

## 2026-10-05 Jake: every gadget violet (+ ring icons)
- Gadget ids -> ring icon palette rows at x 944 (found by writing slot 0x800F51A9 and reading the 0x65 sprite at
  270,39): 1 net 448, 2 radar 459, 3 sling 455, 4 hoop 451, 5 punch 454, 6 flyer 458, 7 RC car 457, 0/8 club 456
  (9 -> 452, a brown ball, unknown, left alone).
- Gadget textures (menu only): palettes 944,509/491/505/504/502 (502/509 reds = car/punch, 504 greens = flyer,
  505 gold, 491 grey + red light); 469 greys untouched. All in JAKE_CLUT_SWAPS (hue 265, sat >= 0.25 only).
- Models: JAKE_MODEL_TINTS = 10 table entries 0x800EC060..170 by (count, first, second colour) signature ->
  violet copy, greys kept (jake_purple_colour keep_greys). SMG (gadgets mod) in 0x800EC090 fails the signature
  so it stays; after power-down the violet list pointer comes back (verified).
- 2026-10-05 user tweak: Magic Punch only the red glove (spring = texture 944,502 left stock), RC car only reds
  (body 944,509 reds only; gold 944,505 left stock), punch/car icons reds only; radar already kept its greys.
  Model entries mapped by flashing each green in the gadget menu: 060 club handle, 080 radar stand, 090 slingshot,
  100 punch glove, 150 radar dish; 0B0/0E0/130/140/170 not in the menu -> reds only (JAKE_TINT_REDS).
- 2026-10-05 Sky Flyer: flight spin glow = palette 944,470 (all greens) -> violet. Flight sparkles = translucent
  sprites, palette 704,505 x a teal tint from the particle array (24 x 0x66 at 0x800E07F0, colour u16 +0x30/32/34);
  user chose to KEEP the green sparkles, so no sparkle change. Orb haze palette 704,498 is greyscale (generic glow).
  Note: heavy back-to-back gpu_frame_dump calls trigger the slow_frames watchdog dump and stall the debug server.
