# build.ps1 - build FireRed Remaster with MSYS2 mingw64 + CMake + Ninja.
# Output: build\FireRed Remaster.exe   (tools\deploy.bat copies it to the game)
param([switch]$Clean)
$ErrorActionPreference = 'Stop'

$Root  = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Src   = Join-Path $Root 'FireRedLeafGreenRecomp'
$Build = Join-Path $Root 'build'
$Mingw = 'C:\msys64\mingw64\bin'
if (-not (Test-Path (Join-Path $Mingw 'c++.exe'))) {
    throw "Compiler not found in $Mingw - run tools\SETUP.bat first."
}
$env:PATH = "$Mingw;$env:PATH"
if ($Clean -and (Test-Path $Build)) { Remove-Item -Recurse -Force $Build }

$fw   = { param($p) $p.Replace('\', '/') }
$sdl  = & $fw (Join-Path $Root 'deps\SDL2-2.32.10\x86_64-w64-mingw32')
$toml = & $fw (Join-Path $Root 'deps\tomlplusplus')
$mg   = & $fw $Mingw

if (-not (Test-Path (Join-Path $Build 'CMakeCache.txt'))) {
    & cmake -S $Src -B $Build -G Ninja `
        "-DCMAKE_C_COMPILER=$mg/cc.exe" `
        "-DCMAKE_CXX_COMPILER=$mg/c++.exe" `
        "-DCMAKE_MAKE_PROGRAM=$mg/ninja.exe" `
        -DCMAKE_BUILD_TYPE=Release `
        "-DCMAKE_C_FLAGS_RELEASE=-O1 -DNDEBUG" `
        "-DCMAKE_CXX_FLAGS_RELEASE=-O1 -DNDEBUG" `
        "-DCMAKE_EXE_LINKER_FLAGS=-static" `
        -DGBARECOMP_BUILD_ORACLE=OFF `
        "-DSDL2_DIR=$sdl/lib/cmake/SDL2" `
        "-DSDL2_INCLUDE_DIR=$sdl/include/SDL2" `
        "-DSDL2_LIBRARY=$sdl/lib/libSDL2.dll.a" `
        "-DFETCHCONTENT_SOURCE_DIR_TOMLPLUSPLUS=$toml" `
        -DFETCHCONTENT_FULLY_DISCONNECTED=ON
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed ($LASTEXITCODE)" }
}

& cmake --build $Build --target FireRedRecomp
if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)" }

$exe = Join-Path $Build 'FireRed Remaster.exe'
if (-not (Test-Path $exe)) { throw "Build finished but $exe is missing" }
Write-Host ""
Write-Host "Built: $exe"
# Silent screenshot checks (capture-3d.ps1) run this copy.
$check = Join-Path $Root 'tools\check-game'
if (Test-Path $check) { Copy-Item $exe (Join-Path $check 'FireRedCheck.exe') -Force }
Write-Host "Next: tools\deploy.bat (close the game first)"
