# PSX-Authentic Engine build script (MinGW-w64 g++)
# Usage: .\build.ps1 [-Assets] [-Run] [-Clean] [-Debug]
param(
    [switch]$Assets,   # rebuild game assets via Python pipeline
    [switch]$Run,      # run the demo after building
    [switch]$Clean,    # remove build outputs first
    [switch]$Debug     # -O0 -g instead of -O2
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$sdlVer = '3.4.12'
$sdl  = Join-Path $root "external\SDL3-$sdlVer\x86_64-w64-mingw32"
$bin  = Join-Path $root 'build\bin'

# Fetch the SDL3 MinGW dev package on first build (not committed to the repo).
if (-not (Test-Path (Join-Path $sdl 'lib\libSDL3.dll.a'))) {
    Write-Host '=== Downloading SDL3 (first build) ===' -ForegroundColor Cyan
    $ext = Join-Path $root 'external'
    New-Item -ItemType Directory -Force $ext | Out-Null
    $zip = Join-Path $ext 'SDL3-devel-mingw.zip'
    $url = "https://github.com/libsdl-org/SDL/releases/download/release-$sdlVer/SDL3-devel-$sdlVer-mingw.zip"
    Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing
    Expand-Archive -Path $zip -DestinationPath $ext -Force
    if (-not (Test-Path (Join-Path $sdl 'lib\libSDL3.dll.a'))) { throw 'SDL3 download/extract failed' }
}

if ($Clean) {
    Remove-Item -Recurse -Force (Join-Path $root 'build\bin\*') -ErrorAction SilentlyContinue
    Remove-Item -Recurse -Force (Join-Path $root 'build\assets\*') -ErrorAction SilentlyContinue
}

New-Item -ItemType Directory -Force $bin | Out-Null

if ($Assets) {
    Write-Host '=== Building assets ===' -ForegroundColor Cyan
    python (Join-Path $root 'tools\build_assets.py')
    if ($LASTEXITCODE -ne 0) { throw 'asset build failed' }
}

Write-Host '=== Compiling engine ===' -ForegroundColor Cyan
$sources = @(
    Get-ChildItem (Join-Path $root 'engine') -Recurse -Filter *.cpp | ForEach-Object { $_.FullName }
    Get-ChildItem (Join-Path $root 'game\src') -Recurse -Filter *.cpp | ForEach-Object { $_.FullName }
)

$opt = if ($Debug) { @('-O0','-g') } else { @('-O2') }
$gccArgs = @(
    $opt
    '-std=c++17'
    '-Wall','-Wextra','-Wno-unused-parameter'
    "-I$root"
    "-I$sdl\include"
    $sources
    "-L$sdl\lib"
    '-lSDL3'
    '-lm'
    '-o', (Join-Path $bin 'psx_demo.exe')
) | ForEach-Object { $_ }

& g++ @gccArgs
if ($LASTEXITCODE -ne 0) { throw 'compile failed' }

Copy-Item (Join-Path $sdl 'bin\SDL3.dll') $bin -Force
Write-Host "=== Built $bin\psx_demo.exe ===" -ForegroundColor Green

if ($Run) {
    Push-Location $root
    & (Join-Path $bin 'psx_demo.exe')
    Pop-Location
}
