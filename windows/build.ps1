<#
  Builds Window Sweaters for Windows on x64 and ARM64, runs the renderer
  tests, and packages one zip per architecture in outputs\.

    powershell -ExecutionPolicy Bypass -File windows\build.ps1
    powershell -ExecutionPolicy Bypass -File windows\build.ps1 -Arch ARM64

  Needs CMake and Visual Studio 2022 (or its Build Tools) with the
  "Desktop development with C++" workload, including the ARM64 tools for
  ARM64 builds.
#>
param(
  [ValidateSet('x64', 'ARM64', 'all')] [string]$Arch = 'all',
  [ValidateSet('Release', 'Debug')] [string]$Config = 'Release'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$version = '1.5.0'
$arches = if ($Arch -eq 'all') { @('x64', 'ARM64') } else { @($Arch) }
$host_arch = if ($env:PROCESSOR_ARCHITEW6432) { $env:PROCESSOR_ARCHITEW6432 } else { $env:PROCESSOR_ARCHITECTURE }
$outputs = Join-Path $root 'outputs'
New-Item -ItemType Directory -Force $outputs | Out-Null

foreach ($a in $arches) {
  $build = Join-Path $root "build\$($a.ToLower())"
  Write-Host "== $a ($Config)" -ForegroundColor Cyan
  cmake -S (Join-Path $root 'windows') -B $build -A $a
  if ($LASTEXITCODE) { throw "cmake configure failed for $a" }
  cmake --build $build --config $Config
  if ($LASTEXITCODE) { throw "build failed for $a" }

  # An ARM64 PC runs both builds; an x64 PC cannot run the ARM64 one.
  $can_run = $a -eq 'x64' -or $host_arch -eq 'ARM64'
  if ($can_run) {
    & (Join-Path $build "$Config\render-test.exe")
    if ($LASTEXITCODE) { throw "render tests failed for $a" }
  } else {
    Write-Host "Skipping $a tests: this PC cannot run $a code." -ForegroundColor Yellow
  }

  $stage = Join-Path $build 'package\Window Sweaters'
  Remove-Item -Recurse -Force (Split-Path $stage) -ErrorAction SilentlyContinue
  New-Item -ItemType Directory -Force $stage | Out-Null
  Copy-Item (Join-Path $build "$Config\WindowSweaters.exe") $stage
  Copy-Item (Join-Path $root 'windows\README.md') (Join-Path $stage 'README.md')
  Copy-Item (Join-Path $root 'LICENSE') $stage
  Copy-Item (Join-Path $root 'NOTICE.md') $stage
  $zip = Join-Path $outputs "WindowSweaters-$version-windows-$($a.ToLower()).zip"
  Remove-Item -Force $zip -ErrorAction SilentlyContinue
  Compress-Archive -Path $stage -DestinationPath $zip
  Write-Host "Packaged $zip" -ForegroundColor Green
}
