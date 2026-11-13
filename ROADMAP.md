# Roadmap

## Next
- Input without a script: windowed, DirectInput still has to acquire the
  real mouse and keyboard (scripted runs replace the devices' state).
- Back onto pcrecomp `main` once `pcrecomp-stp`'s native32 work is merged there.
- Sound: check DirectSound by ear (`--sound`).
- A conformance harness (REPO_RULES section 9): title, attract, a stage
  start, as milestones with a pass count and a baseline, in the shape of
  hover's `tools/conformance.py`. It needs the game, so without it it prints
  a SKIP line for netlab's `QA_SKIP`.
- Headless recording: `--headless --record out.mp4` (frames piped to ffmpeg),
  and scripted input (`--key`), as hover's host has.
- Run `Setup.cmd` end to end from a clean machine with the CD, and publish the
  installer (the `installer` workflow, run by hand while the repo is private).

## Then
- The disc-root `Hod2.exe` (stamp 0x3B175404, two hours newer): what changed.
- Two players; the light gun (`\.\SEGAJOY.VXD` is opened and missing).
- The window: scaling beyond 640x480.

## Toolkit (pcrecomp)
- MSVC `setjmp`/`longjmp` into lifted frames, next to HotD 1's Watcom one.
- The host name and the manifest both turn off Windows' 16-bit DirectDraw
  modes: worth a note for every D3D/DDraw recomp.

## Out of scope
- The Activision intro movie, unless an Indeo 3.2 decoder is installed: the
  game is told it ended.
- Shipping any game file or generated source.
