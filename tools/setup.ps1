# One-click setup for The House of the Dead 2 (PC) static recompilation. Run it
# by double-clicking Setup.cmd in the repo folder; the README's "Step by step" is
# the same thing by hand (docs/building.md), command for command.
#
# It checks the tools, copies your copy of the game into game\, finds every
# function, lifts Hod2.exe to C, builds build\hotd2recomp.exe and leaves a
# shortcut to it in this folder. Each step is skipped when its output already
# exists (-Force redoes them), so a rerun after a failure resumes. Everything it
# does goes to setup.log. Nothing of the game is downloaded: it comes from your
# CD (or an image or copy of it).
#
#   -GameDir <drive, folder, .iso or .cue>   skip the question (-Game works too)
#   -Yes                               answer yes to every install question
#
# The toolkit is ..\pcrecomp (PCRECOMP overrides it), on its pcrecomp-stp
# branch: the guest registry, synthetic DirectInput and DirectDraw fixes this
# game plays on are not on pcrecomp main yet. Under recomp-launcher
# (RECOMP_LAUNCHER=1) it never waits for Enter, and exit code 0 means the
# shortcut is there.
param([switch]$Force, [switch]$Yes, [Alias('Game')][string]$GameDir = "")
$Game = $GameDir

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$Branch = 'pcrecomp-stp'
$Toolkit = $env:PCRECOMP
if (-not $Toolkit) { $Toolkit = Join-Path (Split-Path -Parent $Root) 'pcrecomp' }
$env:PCRECOMP = $Toolkit   # run_lift.py reads it too
$Log = Join-Path $Root 'setup.log'
Set-Location $Root
function Log($t) { Add-Content -Path $Log -Value $t -Encoding UTF8 }
Log "==== setup $(Get-Date -Format s)"

function Say($t, $c = 'Gray') { Write-Host $t -ForegroundColor $c; Log $t }
function Step($n, $t) { Write-Host ""; Say "[$n/6] $t" 'Cyan' }
function Pause-End { if (-not $Yes -and $env:RECOMP_LAUNCHER -ne '1') { Read-Host "Press Enter to close" | Out-Null } }
function Fail($t) {
  Say ""; Say "Setup stopped: $t" 'Red'
  Say "The details are in $Log. Fix that and run Setup.cmd again; finished steps are skipped." 'Yellow'
  Pause-End; exit 1
}
trap { Fail "$_" }
function Ask($q) { if ($Yes) { Say "$q yes"; return $true }; $a = Read-Host "$q [Y/n]"; return -not ($a -match '^[nN]') }
function Refresh-Path {
  $env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User')
}
# Run a program with its output in the log. Windows PowerShell turns a native
# program's stderr into errors, so 'Stop' is off while it runs.
function Exec([string[]]$cmd) {
  $old = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
  $rest = @($cmd | Select-Object -Skip 1)
  & $cmd[0] @rest 2>&1 | ForEach-Object { Log "$_" }
  $code = $LASTEXITCODE
  $ErrorActionPreference = $old
  return $code
}
function Run($what, [string[]]$cmd) {
  Say "  $what..."
  $code = Exec $cmd
  if ($code -ne 0) { Fail "$what failed (exit code $code)." }
}

if ($env:RECOMP_LAUNCHER -ne '1') { Clear-Host }
Say "The House of the Dead 2 (PC): static recompilation setup" 'White'
Say "You need your own copy of the game (the CD, an .iso of it, or a folder copy of it) and about 2 GB free."

# ---------------------------------------------------------------- tools
Step 1 "Checking the tools"
# A Python that answers "Python 3.x". The Store's placeholder "python" answers
# nothing, so asking is the reliable test.
function Find-Python {
  $user = @(Get-ChildItem "$env:LOCALAPPDATA\Programs\Python\Python3*\python.exe" -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | ForEach-Object { ,@($_.FullName) })
  foreach ($c in (@(@('py', '-3'), @('python'), @('python3')) + $user)) {
    if (-not (Get-Command $c[0] -ErrorAction SilentlyContinue)) { continue }
    $old = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    $rest = @($c | Select-Object -Skip 1)
    $v = (& $c[0] @rest --version 2>&1 | Out-String).Trim()
    $ErrorActionPreference = $old
    if ($v -match '^Python 3\.(\d+)' -and [int]$Matches[1] -ge 10) { return ,$c }
  }
  return $null
}
$pyargs = Find-Python
if (-not $pyargs) {
  Say "  Python 3.10 or newer is not installed."
  if (-not (Ask "  Install Python 3.12 now (winget, for your user only, about 30 MB)?")) { Fail "Python 3 is required." }
  if (-not (Get-Command winget -ErrorAction SilentlyContinue)) { Fail "winget is missing. Install 'App Installer' from the Microsoft Store, or install Python yourself." }
  Exec @('winget', 'install', '-e', '--id', 'Python.Python.3.12', '--scope', 'user', '--accept-package-agreements', '--accept-source-agreements') | Out-Null
  Refresh-Path
  $pyargs = Find-Python
  if (-not $pyargs) { Fail "Python installed, but it can't be found. Install Python 3.12 from python.org yourself." }
}
Say "  Python: $($pyargs -join ' ')"

if ((Exec ($pyargs + @('-c', 'import pefile, capstone'))) -ne 0) {
  Say "  The Python packages pefile and capstone are missing (about 20 MB)."
  if (-not (Ask "  Install them now (pip, for your user only)?")) { Fail "pefile and capstone are required." }
  Run "Installing pefile and capstone" ($pyargs + @('-m', 'pip', 'install', '--user', 'pefile', 'capstone'))
}

if (-not (Test-Path (Join-Path $Toolkit 'tools\lift\lift32.py'))) {
  Say "  The pcrecomp toolkit is not beside this folder ($Toolkit)."
  if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    if (-not (Ask "  git is needed to fetch it. Install Git for Windows now (winget, about 60 MB)?")) { Fail "git is needed to fetch pcrecomp." }
    if (-not (Get-Command winget -ErrorAction SilentlyContinue)) { Fail "winget is missing. Install Git for Windows yourself." }
    Exec @('winget', 'install', '-e', '--id', 'Git.Git', '--accept-package-agreements', '--accept-source-agreements') | Out-Null
    Refresh-Path
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) { Fail "Git did not install. Install Git for Windows yourself." }
  }
  if (-not (Ask "  Download it now (git, about 20 MB)?")) { Fail "pcrecomp is required at $Toolkit." }
  Run "Cloning pcrecomp (branch $Branch)" @('git', 'clone', '--depth', '1', '-b', $Branch, 'https://github.com/sp00nznet/pcrecomp', $Toolkit)
}
# PCRECOMP_MUTE is in pcrecomp-stp's native32 and not in main's: the marker
# that this clone has the branch's runtime.
$stpOk = { Select-String -Quiet -SimpleMatch 'PCRECOMP_MUTE' (Join-Path $Toolkit 'runtime\native32\native32.c') }
if (-not (& $stpOk) -and (Test-Path (Join-Path $Toolkit '.git'))) {
  Say "  Your pcrecomp is not on the $Branch branch this game needs."
  if (Ask "  Switch it to $Branch now (git fetch and checkout)?") {
    Run "Fetching $Branch" @('git', '-C', $Toolkit, 'fetch', 'origin', "${Branch}:${Branch}")
    Run "Switching to $Branch" @('git', '-C', $Toolkit, 'checkout', $Branch)
  }
}
if (-not (& $stpOk)) { Fail "your pcrecomp at $Toolkit is not on the $Branch branch. Switch it: git -C `"$Toolkit`" checkout $Branch" }
Say "  pcrecomp: $Toolkit ($Branch)"

# Visual Studio 2022 or its Build Tools, with the C++ x86 compiler; CMake and
# Ninja ship inside that workload, so build.cmd needs nothing else.
function Find-VS {
  $p = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
  if (Test-Path $p) { return (& $p -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath) }
  return $null
}
if (-not (Find-VS)) {
  Say "  Visual Studio 2022 with the C++ tools is not installed. The recompiled game is C, and this is the compiler."
  if (-not (Ask "  Install the Visual Studio 2022 Build Tools with the C++ workload now (winget, about 3 GB)?")) { Fail "a C compiler (Visual Studio 2022 C++) is required." }
  if (-not (Get-Command winget -ErrorAction SilentlyContinue)) { Fail "winget is missing. Install Visual Studio 2022 Build Tools with 'Desktop development with C++' yourself." }
  Exec @('winget', 'install', '-e', '--id', 'Microsoft.VisualStudio.2022.BuildTools', '--accept-package-agreements', '--accept-source-agreements',
         '--override', '--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended') | Out-Null
  if (-not (Find-VS)) { Fail "the Build Tools did not install. Install Visual Studio 2022 with 'Desktop development with C++' yourself." }
}
Say "  C compiler: $(Find-VS)"

# ---------------------------------------------------------------- the game
Step 2 "Copying your copy of the game into game\"
if ((Test-Path 'game\Hod2.exe') -and (Test-Path 'game\hod2000.arc') -and -not $Force) {
  Say "  Already there (skipping)."
} else {
  $Game = $Game.Trim('"', ' ')
  if (-not $Game) {
    # The CD answers for itself: HOD2.ICO at its root.
    foreach ($d in (Get-PSDrive -PSProvider FileSystem)) {
      if (Test-Path (Join-Path $d.Root 'HOD2.ICO')) { $Game = $d.Root; Say "  Found the CD in $Game"; break }
    }
  }
  if (-not $Game -and $Yes) { Fail "The game wasn't found. Give its drive, folder, .iso or .cue with -GameDir." }
  while (-not $Game) {
    $Game = (Read-Host "  Where is the game? The CD's drive (E:\), a folder copy of the CD, an .iso or a .cue").Trim('"', ' ')
  }
  # A .cue/.bin image becomes an .iso (its data track), which Windows mounts.
  if ($Game -match '\.cue$') {
    New-Item -ItemType Directory -Force 'work' | Out-Null
    if (-not (Test-Path 'work\disc.iso') -or $Force) { Run "Converting $(Split-Path -Leaf $Game) to work\disc.iso" ($pyargs + @('tools\cue2iso.py', $Game, 'work\disc.iso')) }
    $Game = Join-Path $Root 'work\disc.iso'
  }
  $img = $null
  if ($Game -match '\.iso$') {
    Say "  Mounting $Game"
    $iso = (Resolve-Path $Game).Path
    $img = Get-DiskImage -ImagePath $iso
    if (-not $img.Attached) { $img = Mount-DiskImage -ImagePath $iso -PassThru }
    $Game = (Get-Volume -DiskImage $img).DriveLetter + ':\'
  }
  if (-not (Test-Path $Game)) { Fail "$Game does not exist." }
  # The installed files: from the CD's .msi unpacked (an administrative install
  # copies the files without installing anything), or from a folder copy of an
  # install. The CD's own loose Hod2.exe is a different build from the installed
  # one, so the .msi comes first.
  $msi = Get-ChildItem $Game -Filter '*.msi' -ErrorAction SilentlyContinue | Select-Object -First 1
  if (-not $msi) {
    $exe = Get-ChildItem $Game -Recurse -Depth 3 -Filter 'Hod2.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $exe) { Fail "neither the game's .msi nor Hod2.exe is in $Game." }
  } else {
    New-Item -ItemType Directory -Force 'work\msi' | Out-Null
    Run "Unpacking $($msi.Name) (no install)" @('msiexec', '/a', $msi.FullName, "TARGETDIR=$Root\work\msi", '/qn')
    $exe = Get-ChildItem 'work\msi' -Recurse -Filter 'Hod2.exe' | Select-Object -First 1
    if (-not $exe) { Fail "the .msi unpacked, but there is no Hod2.exe in it." }
  }
  Say "  Copying the game from $($exe.DirectoryName)..."
  New-Item -ItemType Directory -Force 'game' | Out-Null
  Copy-Item -Path (Join-Path $exe.DirectoryName '*') -Destination 'game' -Recurse -Force
  # The disc's own files the game opens on "its CD" (docs\lifting.md, section 1).
  foreach ($f in @('HOD2.ICO', 'hod2000.arc')) {
    if (Test-Path (Join-Path 'game' $f)) { continue }
    $src = Get-ChildItem $Game -Recurse -Depth 2 -Filter $f -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $src) { Fail "$f is not in $Game; it is at the root of the CD." }
    Copy-Item $src.FullName 'game'
  }
  # Left mounted: the game checks for its CD each time it starts.
  if ($img) { Say "  $($img.ImagePath) stays mounted as $Game for playing." }
  # game\ has it all now; the unpacked .msi is 700 MB of copies.
  Remove-Item -Recurse -Force 'work\msi' -ErrorAction SilentlyContinue
}
New-Item -ItemType Directory -Force 'work' | Out-Null
Copy-Item 'game\Hod2.exe' 'work\Hod2.exe' -Force

# ---------------------------------------------------------------- catalog
Step 3 "Finding every function (a minute or two)"
if ((Test-Path 'work\functions.json') -and -not $Force) { Say "  Already done (skipping)." }
else { Run "Disassembling Hod2.exe" ($pyargs + @("$Toolkit\tools\disasm\disasm32.py", 'work\Hod2.exe', '-o', 'work\functions.json')) }

# ---------------------------------------------------------------- lift
Step 4 "Recompiling Hod2.exe to C"
if ((Test-Path 'src\recomp\gen\recomp_dispatch.c') -and -not $Force) { Say "  Already done (skipping)." }
else { Run "Lifting" ($pyargs + @('run_lift.py', '--all')) }

# ---------------------------------------------------------------- build
Step 5 "Building build\hotd2recomp.exe (about 10 minutes the first time)"
if ((Test-Path 'build\hotd2recomp.exe') -and -not $Force) { Say "  Already built (skipping)." }
else {
  $env:CMAKE_ARGS = "-DPCRECOMP=$($Toolkit -replace '\\', '/')"
  Run "Compiling" @('cmd', '/d', '/c', "`"$Root\build.cmd`"")
}

# ---------------------------------------------------------------- shortcut
Step 6 "Making the shortcut"
$lnk = Join-Path $Root 'The House of the Dead 2.lnk'
$s = (New-Object -ComObject WScript.Shell).CreateShortcut($lnk)
$s.TargetPath = Join-Path $Root 'build\hotd2recomp.exe'
$s.Arguments = '--run --sound'
$s.WorkingDirectory = $Root
$s.IconLocation = (Join-Path $Root 'game\HOD2.ICO') + ',0'
$s.Save()
if (-not (Test-Path $lnk)) { Fail "the shortcut was not made." }
Say "  $lnk"
Write-Host ""
Say "Done. Double-click 'The House of the Dead 2' in this folder to run it. It reaches the title and the attract demo; playing by hand is not working yet (README, Status)." 'Green'
Pause-End
exit 0
