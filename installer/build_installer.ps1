# Builds dist\CreativeCell_Setup_<version>.exe from the Release build.
#   powershell -ExecutionPolicy Bypass -File installer\build_installer.ps1 [-Makensis <path>]
# Needs NSIS 3 (makensis.exe): in .tools\nsis, on PATH, in Program Files, or
# given with -Makensis. The version comes from CMakeLists.txt.
param([string]$Makensis = "")

$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent $here

if (-not $Makensis) {
  $local = Join-Path $root ".tools\nsis\makensis.exe"
  if (Test-Path $local) { $Makensis = $local }
}
if (-not $Makensis) {
  $cmd = Get-Command makensis.exe -ErrorAction SilentlyContinue
  if ($cmd) { $Makensis = $cmd.Source }
}
if (-not $Makensis) {
  foreach ($p in @("${env:ProgramFiles(x86)}\NSIS\makensis.exe", "$env:ProgramFiles\NSIS\makensis.exe")) {
    if (Test-Path $p) { $Makensis = $p; break }
  }
}
if (-not $Makensis -or -not (Test-Path $Makensis)) { throw "makensis.exe not found - install NSIS 3 or pass -Makensis <path>" }

foreach ($f in @("CreativeCell.exe", "cc_thumbs.dll")) {
  if (-not (Test-Path (Join-Path $root "build\bin\Release\$f"))) {
    throw "Build the Release configuration first: cmake --build build --config Release ($f missing)"
  }
}

$cmake = Get-Content (Join-Path $root "CMakeLists.txt") -Raw
if ($cmake -notmatch 'project\(creative_cell VERSION (\d+\.\d+\.\d+)') { throw "No version in CMakeLists.txt" }
$version = $Matches[1]

New-Item -ItemType Directory -Force (Join-Path $root "dist") | Out-Null
& $Makensis /V2 "/DAPP_VERSION=$version" (Join-Path $here "creative_cell.nsi")
if ($LASTEXITCODE -ne 0) { throw "makensis failed ($LASTEXITCODE)" }
Get-ChildItem (Join-Path $root "dist\*.exe") | Select-Object Name, Length, LastWriteTime
