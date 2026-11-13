#!/bin/sh
# Linux and macOS: not possible yet. The host runs on pcrecomp's native32,
# which is Windows-only (it calls the real DirectDraw, Direct3D, DirectInput
# and DirectSound), and the build has not been tried under Wine.
echo "The House of the Dead 2 recomp builds and runs on Windows only for now: use Setup.cmd there." >&2
exit 1
