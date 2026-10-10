$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if (!(Test-Path 'C:/msys64/mingw64/bin/gcc.exe')) {
    throw 'Requires the existing project MSYS2 MinGW64 compiler at C:/msys64/mingw64/bin/gcc.exe. Install from https://www.msys2.org/ first.'
}
$installer=Join-Path $PSScriptRoot 'rustup-init.exe'
Invoke-WebRequest 'https://static.rust-lang.org/rustup/dist/x86_64-pc-windows-gnu/rustup-init.exe' -OutFile $installer
$env:RUSTUP_HOME=Join-Path $PSScriptRoot 'rustup'
$env:CARGO_HOME=Join-Path $PSScriptRoot 'cargo'
& $installer -y --no-modify-path --profile minimal --default-host x86_64-pc-windows-gnu --default-toolchain stable
if ($LASTEXITCODE) {throw 'Rust installation failed'}

