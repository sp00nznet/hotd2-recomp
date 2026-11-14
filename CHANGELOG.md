# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- `Setup.cmd` (`tools/setup.ps1`): checks and offers to install the tools,
  copies the game from your CD, an .iso or a folder copy (unpacking the .msi
  if needed), catalogs, lifts, builds and makes a shortcut.
- `launcher/launcher.json` and an `installer` workflow (run by hand) for
  recomp-launcher; a netlab recipe, `hotd2-recomp`.
- `docs/architecture.md`, `docs/building.md`, `NOTICE.md`, `CONTRIBUTING.md`,
  `CONTRIBUTORS.md`; `setup.sh`, which says Linux and macOS aren't supported
  yet and exits.
- Stage 1 played by scripted input: `--key` and `--click`, up to 8 `--shot`s, and a see-through `--headless`.
- The host relaunches itself under Windows' DWM8And16BitMitigation and
  16BITCOLOR layers, for the 16-bit display modes D3DX wants; the lifted C is
  built at /Od.

### Changed
- Builds against pcrecomp main; the `pcrecomp-stp` branch is merged.
- The CD check runs against a real CD-ROM drive: your disc, or your disc
  image mounted (`CD_DRIVE` names the drive). Setup leaves your image mounted.
- README rewritten in the pcrecomp house style.
- Setup, the launcher and the docs use pcrecomp's `pcrecomp-stp` branch, the
  one this game plays on; Setup offers to switch an existing clone to it.
- Setup unpacks the CD's `.msi` instead of using its loose `Hod2.exe` (a
  different build), and takes a `.cue`/`.bin`.

## [0.1.0] - 2026-11-08

### Added
- First playable: `Hod2.exe` lifted to C (4,832 functions, 0 errors) on
  pcrecomp's native32 host. It boots past the CD check and D3DX's device
  setup to the SEGA logo and the title, and the attract demo plays stage 1.
- Host: windowed D3DX by default (`--fullscreen` for the
  original); the device prompt answered; DirectInput's instance; the Indeo
  intro skipped when there is no decoder; MSVC `longjmp` into lifted frames.
- `docs/lifting.md`.
