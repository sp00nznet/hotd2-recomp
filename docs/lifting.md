# Lifting The House of the Dead 2: what it took

The host is HotD 1's, trimmed: pcrecomp's `native32` maps `Hod2.exe` at its
own base in a 32-bit process and lets every import reach the real DLL. What
follows is everything this game needed on top, in the order it turned up.

## 1. The CD

The game lists the drives, takes a CD-ROM, and opens `hod2.ico` on it. The
host passes those calls to Windows unchanged, so the CD check passes with your
disc in the drive or your disc image mounted. The host finds that drive
(`CD_DRIVE=E`, or the first CD-ROM drive with `HOD2.ICO` at its root) and reads
the files the game opens on it from `game\`, which holds the disc's
`HOD2.ICO` and `hod2000.arc` next to the installed files.

## 2. No 16-bit display modes

D3DX 7 threw "GetCapsAtVideoMode - Couldn't obtain info for this display
mode": the game wants 640x480x16, and DirectDraw offered this process only
32-bit modes. Windows emulates 8- and 16-bit modes only for programs that
need it, and two things turned it off here:

- **A manifest declaring Windows 8 or later** (HotD 1's host has one). A test
  program saw 81 modes without it and 27 with it.
- **The name `hotd2.exe`.** Windows' compatibility database matches it: the
  same binary renamed got the 16-bit modes back. The host is `hotd2recomp.exe`.

D3DX caches device caps in `%windir%\d3dx.dat` and trusts the cache, so the
host keeps the game's copy in `game\` (a cache written while modes were
missing has to be deleted).

## 3. The device prompt

"Attention: *device* will be used" is a Yes/No box, asked per device until
one is taken (`sub_004B23A0`). The host answers Yes.

## 4. Windowed, not fullscreen

D3DX's fullscreen context throws on the first focus change (Flip returns
`DDERR_SURFACELOST`), and it changes the monitor's mode. D3DX supports
windowed contexts itself: its mode is `flags & 3` stored at
`0x004BD017` (0 windowed, 1 fullscreen, 2 offscreen), and a patch there makes
it 0 unless `--fullscreen`. The game pauses while inactive, so the host's
window procedure tells it it is always active.

DirectInput refused to start (`E_INVALIDARG`): the game passes its own image
base as the instance, which Windows never loaded. The host passes its own.

## 5. The intro movie

The game first plays `Activision Value Intro.avi` through DirectShow
(`sub_0041E0B0`, states 3-5) and polls until it ends. It is Indeo 3.2,
which Windows no longer ships, so it never ends: a black screen with the
SEGA logo frames loaded behind it. A patch on the poll (`0x0041E080`)
reports it over when no Indeo 3.2 decoder is installed.

## 6. MSVC's setjmp and longjmp

The task tree (`sub_004AC240`) runs each task under `setjmp` (`0x004C529C`)
and a task ends with `longjmp` (`0x004C5220`). A lifted function can't be
returned into by a restored `esp`, so as for Watcom's in HotD 1: every call
to `setjmp` gets a landing pad, and a patch on `longjmp` restores the
registers from the buffer (MSVC's layout: ebp, ebx, edi, esi, esp, return
address) and resumes the owning lifted frame at its pad. The buffer's return
address is 0 under lifting (no real return addresses are pushed), so the pad
is found by the owning function.

## Debugging aids

| | |
|---|---|
| `[throw]` | every C++ throw: its type, and D3DX's message |
| `--shot S FILE` | the game's window (only it) after S seconds |
| `--sample` | the lifted function running, ten times a second |
| `--native-trace`, `--callbacks` | every call out to Windows, every call back in |
