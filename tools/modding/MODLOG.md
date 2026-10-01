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
