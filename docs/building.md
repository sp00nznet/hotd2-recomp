# Building The House of the Dead 2 recomp

`Setup.cmd` (`tools/setup.ps1`) runs exactly these steps, skipping finished ones.

## Step by step

Prerequisites: Windows 10/11, **Python 3.10+** (`py -3 --version`), **git**,
**Visual Studio 2022** (or its Build Tools) with *Desktop development with
C++*, and the pcrecomp toolkit cloned **beside** this repository as `pcrecomp`,
on its **`pcrecomp-stp`** branch (the guest registry, synthetic DirectInput and DirectDraw fixes this game plays on are not on pcrecomp `main` yet):

```
some-folder\
  pcrecomp\                     <- git clone -b pcrecomp-stp https://github.com/sp00nznet/pcrecomp
  hotd2\                        <- this repository
```

1. Python packages:
   ```
   py -3 -m pip install --user pefile capstone
   ```
2. The game files. Copy the installed `THE HOUSE OF THE DEAD 2` folder's
   contents into `game\` (if you have only the CD, unpack its installer without
   installing: `msiexec /a "E:\THE HOUSE OF THE DEAD 2.msi" TARGETDIR=%CD%\work\msi /qn`
   and copy from there). From the CD's root add `HOD2.ICO` and `hod2000.arc`
   to `game\`. Then `copy game\Hod2.exe work\`.
3. The function catalog:
   ```
   py -3 ..\pcrecomp\tools\disasm\disasm32.py work\Hod2.exe -o work\functions.json
   ```
4. Recompile to C:
   ```
   py -3 run_lift.py --all
   ```
   Expected: 4,832 functions lifted, 0 errors.
5. Build (about 10 minutes the first time; the 64-bit-hosted MSVC targeting x86,
   CMake and Ninja all come with the C++ workload):
   ```
   build.cmd
   ```
6. Check it without opening a window:
   ```
   build\hotd2recomp.exe
   ```
   Expected, at the end: `(dry run: image mapped and bound; --run enters 0x...)`.
7. Run: `build\hotd2recomp.exe --run`.

The usual trip-ups: `python` opening the Microsoft Store (that is Windows' alias
placeholder; use `py -3`, or turn the alias off in *Settings > Apps > Advanced
app settings > App execution aliases*); a freshly installed Python not being on
`PATH` until you open a new window; and a stale `game\d3dx.dat` written while
the 16-bit display modes were missing, which has to be deleted
([docs/lifting.md](lifting.md), section 2).

## Usage

```
build\hotd2recomp.exe --run                          # windowed, muted
build\hotd2recomp.exe --run --sound                  # with sound
build\hotd2recomp.exe --run --headless --watchdog 60 # no visible window; stop after 60 s
build\hotd2recomp.exe --run --shot 25 s25.bmp        # the game's window as a .bmp after 25 s
```

A scripted run into stage 1: Start at 10 s (while the title is up; the
attract demo ignores it), then fire twice a second and reload every third:

```
build\hotd2recomp.exe --run --headless --watchdog 60 --key 10 0x1C 200 ^
  --click 14 200 180 120 --click 14.33 200 180 120 --click 15 297 233 120 ... --click 16.66 -100 -100 120 ... --shot 40 stage1.bmp
```

| Flag | |
|---|---|
| `--run` | enter the game (without it: map, bind and stop) |
| `--fullscreen` | the original's fullscreen D3DX context |
| `--sound` | unmute (runs are muted by default) |
| `--headless` | no visible window |
| `--shot S FILE` | save the game's window as a .bmp after S seconds (up to 8) |
| `--key S DIK MS` | at S seconds, hold key DIK (a DirectInput scan code; 0x1C is Enter, Start) for MS ms |
| `--click S X Y MS` | at S seconds, aim at X,Y of the 640x480 screen and hold the trigger for MS ms; outside the screen reloads. `--key` and `--click` go in time order |
| `--watchdog S` | stop after S seconds and say where the game was |
| `--exe FILE`, `--game DIR` | `Hod2.exe` to map (default `work\Hod2.exe`) and the game folder (default `game`) |
| `--args "..."` | switches passed to the game itself |
| `--sample`, `--native-trace`, `--callbacks` | where the game spends its time; one line per call into Windows, or back from it |

## Building from source

Steps 3 to 5 above. `PCRECOMP` (Python) and `-DPCRECOMP=` (CMake, through
`set CMAKE_ARGS=...` for `build.cmd`) point at a toolkit checkout other than
`..\pcrecomp`; the lifter and the runtime must come from the same tree. On
[netlab](https://github.com/sp00nznet/recomp-netlab) the recipe is
`hotd2-recomp`: `netlab build hotd2-recomp`. How the pieces fit is
[docs/architecture.md](architecture.md); what this game needed is
[docs/lifting.md](lifting.md).
