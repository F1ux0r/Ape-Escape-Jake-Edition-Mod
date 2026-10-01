# Ape Escape (PSXRecomp) modding plan
- Install: D:\Ape Escape Recomp\ApeEscapeRecomp-v0.3.0-windows-x64 (standalone, not a store install), v0.3.0
- Engine: PSXRecomp (github.com/mstan/psxrecomp): PS1 MIPS code statically recompiled to native C, running on
  a PS1 hardware simulation. `ApeEscapeRecomp.exe`, x64, native. Game: Ape Escape USA, SCUS-94423.
- Anti-cheat / online: none, single-player only. OK to mod.
- License: PolyForm Noncommercial 1.0.0. Mods must stay non-commercial; never redistribute disc data.
- Saves: `<exe>\saves` (memory cards, save states; created on first launch)
  Config: `game.toml`, `input.ini`, `keybinds.ini`; launcher settings saved next to the exe
  Logs: `psx_last_run_report.json` (crash/exit report)
- Debug: TCP debug server, `debug_port = 4480` in game.toml, with a 36,000-frame (~10 min) state history
  (`overlay_toolchain/include/debug_server.h`). Might only be in debug builds; check once the game runs.

## Built-in mod system (the route to use)
- Packages are folders with a `manifest.toml` (format_version 5). The launcher's Mods page lists them.
  - `mods/bundled/`: shipped mods, wiped by each build. Don't edit.
  - `mods/installed/`: user mods, never touched by builds. **Our mods go here.**
- Each manifest has an `[[target]]` (game_id + disc SHA-256), one or more `[[feature]]` toggles, and:
  - `[[patch]] target = "main_exe"`: guarded word patches (address + `expected` bytes + `replace`), applied
    at first dispatch and reapplied after a savestate load. Example: quick-gadget-select patches one MIPS
    instruction at 0x80063B6C. **No compiling needed.**
  - `target = "disc_raw"` patches with a patch file: can replace disc data (textures, models, audio, text).
  - `[[plugin]]`: native C++ callbacks. Only plugins compiled into the exe run, so new plugins mean building
    from the ApeEscapeRecomp repo with the psxrecomp-v4 submodule.
- Headers for plugin work: `overlay_toolchain/include/mod_*.h`.

## Routes, easiest first
1. **Declarative code/data patches** in `mods/installed/<id>/1.0.0/manifest.toml`: gameplay tweaks (speed,
   gadget behaviour, monkey counts, jump height, and so on). Find addresses with the debug server, Ghidra
   on the disc's `SCUS_944.23`, or RAM search on the PS1 address space.
2. **Disc data patches** (`disc_raw`): replacement textures, sounds or text, generated with fal and converted
   to PS1 formats (TIM textures, VAG audio).
3. **Native plugins**: build from source. Needed for new systems, custom rendering, or HD texture packs.

## Chosen route for "Boomerang Stun Club"
Idea: re-selecting the Stun Club toggles a "boomerang mode" (HUD icon glows). While it's on, attacking throws
the club, which flies out, stuns monkeys it hits, and curves back to Spike.

Route 3 (native plugin, built from source), with route 1 patches for small hooks. Declarative patches can
only swap existing instructions; a new projectile, its flight path and a HUD effect need real code.
Plan:
1. Recon in game: find the held-gadget byte (known: 0x800EC2D2, from quick-gadget-select), the club
   swing/hit function, the slingshot pellet spawn (reuse it as the projectile base), and the HUD gadget
   icon draw call.
2. Toggle: on re-select of the club, flip a mod flag. The HUD icon draw gets a pulsing brighter tint.
3. Throw: when the flag is on and the club is used, spawn a projectile (slingshot entity) with the club
   model. Fly outward about N units, then home back to Spike's position; use the club's hit/stun routine
   on contact. The club can't be swung until it's caught.
4. Package as `mods/installed/boomerang-club` with a feature toggle and options (range, speed).
Conflict: quick-gadget-select also claims "press the equipped button again". Declare them incompatible
or use a different toggle input.

## Lab plan
- Back up `saves/` with `um backup` before testing.
- Keep an untouched copy of the release zip. Only add files under `mods/installed/`.
- Run windowed at a fixed size for screenshots (`um win`).

## Unknowns to resolve first
- **The disc isn't set up yet.** There's no saved settings file, and the last run report shows frame 0.
  Needed: the user's own Ape Escape (USA) dump as .cue + .bin, not a "cooked" .iso. The mod loader checks
  the data-track BIN SHA-256 `1ae17e78...852a`, so the dump must match Redump.
- Does the release build expose the debug server on port 4480?
- Exact `disc_raw` patch file format (read psxrecomp source: `ModPatchTarget::DiscRaw`).
- Community: psxrecomp GitHub and the R.A.I.D. Discord (AI-assisted recomp, so AI mods are welcome).
