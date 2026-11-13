# Architecture

Three pieces, two languages, one direction of flow.

```
your CD ──Setup.cmd──> game\  (installed files + HOD2.ICO, hod2000.arc)
                       work\Hod2.exe
pcrecomp disasm32.py ─> work\functions.json          (the function catalog)
run_lift.py          ─> src\recomp\gen\*.c           (the lifted game, gitignored)
build.cmd (CMake)    ─> build\hotd2recomp.exe        (32-bit, MSVC x86)
```

- **`run_lift.py`** (Python) drives pcrecomp's 32-bit lifter (`tools/lift`) over
  every function in the catalog. It also carries this game's patches: C run after
  an instruction at a fixed address (windowed D3DX, the intro-movie poll, the
  `setjmp`/`longjmp` landing pads). The patches are at this build's addresses,
  which is why it refuses any `Hod2.exe` but the one with PE stamp `0x3B173404`.
- **`src/runtime/host.c`** (C) is the host. pcrecomp's `native32` maps
  `Hod2.exe` at its own base, binds its imports to the real Windows DLLs, and
  runs the lifted functions in place of the original code. The host adds the
  shims this game needs (finding the CD drive, the device prompt, DirectInput's
  instance, the window procedure) and the run options (`--headless`, `--shot`,
  `--watchdog`, tracing). Why each one exists: [lifting.md](lifting.md).
- **pcrecomp** (`..\pcrecomp`, or `PCRECOMP` / `-DPCRECOMP=`) supplies the
  lifter and the `native32` and `recomp32` runtime. The lifter and the runtime
  must come from the same checkout: the generated C includes the runtime's
  headers.

The executable is named `hotd2recomp.exe`, not `hotd2.exe`, on purpose: Windows'
compatibility database matches `hotd2.exe` and takes away the 16-bit display
modes D3DX needs ([lifting.md](lifting.md), section 2).
