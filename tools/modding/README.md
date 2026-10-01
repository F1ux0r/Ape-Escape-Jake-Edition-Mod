# Modding notes and tools

Working files for the Power Up Gadgets and Play as Jake mods, kept with the code.

- `MODLOG.md`: the journal (addresses, formats, status, what's owed). Start here.
- `MODDING_PLAN.md`: recon and route.
- `cycle.ps1` / `vsbuild.cmd`: rebuild + relaunch at debug save slot 9 (copies live in `modding\`).
- `state.toml`: the enabled mod features (`build\mods\state.toml`).
- `re/`: debug-server test and RE scripts (`dbg.py` client, `grabtest.py`, `smgtest.py`, `depthtest.py`, ...).

Game-derived files (disc dump, RAM dumps, disassembly, texture dumps) are deliberately not here;
they stay in `modding\re\` on the local disk only.
