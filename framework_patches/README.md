# Framework patches

Small additions to the psxrecomp framework (`psxrecomp-v4`, pinned in
`framework_pins.txt`) that the mods need and upstream doesn't have yet.
CMake applies every `*.patch` here to the framework checkout at configure
time (skipping ones already applied) and stops if one no longer applies, so
a framework bump that conflicts is caught at build time rather than shipping
a mod with a missing feature.

- `0001-mod-raw-disc-sectors.patch`: `psx_mod_read_disc_raw_sectors()`, raw
  2352-byte sectors (Form 2 XA included) from the mounted disc, whatever the
  image format. Play as Jake decodes Jake's hurt "ouch" from the XA audio of
  the cutscene after the Jake Attack 3 race with it.
- `0002-msvc-build-fixes.patch`: lets the framework build with MSVC 14.44
  (VS 2022 Build Tools 17.14). `static constexpr` for two flags a lambda's
  `static_assert` reads (C2131 otherwise), and `strtok_r` mapped to MSVC's
  `strtok_s` in the debug server (LNK2019 otherwise). No behaviour change.

To refresh a patch after editing the framework:
`git -C psxrecomp-v4 diff -- <files> > framework_patches/<name>.patch`
