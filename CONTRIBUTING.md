# Contributing

Issues and pull requests are welcome.

## The one rule

**Nothing from the game goes in the repository.** No game files, no lifted C
(`src/recomp/gen/`), no catalog (`work/`), no disassembly listings, nothing
generated from `Hod2.exe`. The tool ships; its output is made on each
person's machine from their own copy. `.gitignore` covers the usual paths;
check `git status` before you commit anyway. Screenshots of the game running
are fine.

## Before a pull request

1. Build from a clean lift against pcrecomp's `pcrecomp-stp` branch:
   `py -3 run_lift.py --all` (expected: 4,832 functions, 0 errors), then
   `build.cmd`.
2. There is no conformance harness yet. Run the scripted stage-1 run in
   [docs/building.md](docs/building.md) and check its `--shot` pictures show
   the title and the stage.

## Where a fix belongs

- A bug in the lifter, the catalog or the native bridge belongs in
  [pcrecomp](https://github.com/sp00nznet/pcrecomp), not here.
- A fix to the game itself is a patch in `run_lift.py`, or host behavior in
  `src/runtime/host.c`, with a comment that says what it fixes and why.
- A wall you got past goes in `docs/lifting.md`: what it looked like, what it
  was, what moved it.

## Where your code comes from

Contributions must be original or MIT-compatible. Don't port code from a GPL
project (Wine, DOSBox, emulators); running one as a black box to compare is
fine.
