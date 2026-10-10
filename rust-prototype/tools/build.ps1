param([switch]$Release)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$env:RUSTUP_HOME = Join-Path $PSScriptRoot 'rustup'
$env:CARGO_HOME = Join-Path $PSScriptRoot 'cargo'
$env:PATH = "$env:CARGO_HOME\bin;C:\msys64\mingw64\bin;$env:PATH"
Push-Location $root
try {
    cargo run --bin firered-core-check --no-default-features --locked
    if ($LASTEXITCODE) { throw 'Core startup/integration failed' }
    cargo test --lib --no-default-features --locked
    if ($LASTEXITCODE) { throw 'Gameplay checks failed' }
    if ($Release) { cargo build --release --locked; $profile='release' }
    else { cargo build --locked; $profile='debug' }
    if ($LASTEXITCODE) { throw 'Build failed' }
    New-Item -ItemType Directory -Force output/data | Out-Null
    $packageExe = Join-Path $root 'output/FireRed Native 3D Prototype.exe'
    try { Copy-Item "target/$profile/firered-native-prototype.exe" $packageExe -ErrorAction Stop }
    catch [System.IO.IOException] {
        $packageExe = Join-Path $root 'output/FireRed Native 3D Prototype.pending.exe'
        Copy-Item "target/$profile/firered-native-prototype.exe" $packageExe
        Write-Host 'Rust game is open; update staged for the next run.bat launch.'
    }
    Copy-Item "target/$profile/firered-core-check.exe" 'output/FireRed Native Core Check.exe'
    Copy-Item 'data/*.json' 'output/data/'
    Copy-Item -Recurse -Force 'assets' 'output/'
    if (!(Test-Path 'output/config.ini')) {
        "[Prototype]`nbattle_slice=1`nsource_textures=1`nmusic=0`nfree_movement=1`nthird_person=1`ndiagonal_movement=1`ncamera_rotation=1`nsmooth_movement=1`n" | Set-Content 'output/config.ini'
    }
    if ((Get-Content 'output/config.ini' -Raw) -notmatch '(?m)^battle_slice=') {
        Add-Content 'output/config.ini' 'battle_slice=1'
    }
    foreach ($dll in @('libgcc_s_seh-1.dll','libstdc++-6.dll','libwinpthread-1.dll')) {
        Copy-Item "C:/msys64/mingw64/bin/$dll" output
    }
    $process=Start-Process -FilePath $packageExe -ArgumentList '--check' -WindowStyle Hidden -Wait -PassThru
    if ($process.ExitCode) { throw 'Output data validation failed' }
    $process=Start-Process -FilePath $packageExe -ArgumentList '--field-smoke' -WindowStyle Hidden -Wait -PassThru
    if ($process.ExitCode) { throw 'Native field integration failed' }
    $process=Start-Process -FilePath $packageExe -ArgumentList '--scene-smoke' -WindowStyle Hidden -Wait -PassThru
    if ($process.ExitCode) { throw 'Native actor/scene integration failed' }
} finally { Pop-Location }

