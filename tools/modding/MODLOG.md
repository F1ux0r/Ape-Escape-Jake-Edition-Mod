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
