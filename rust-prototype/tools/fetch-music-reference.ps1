$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$reference=Join-Path $root 'reference/gba-audio-tools'
$revision='45e84ba8a5a47b22e56dac7e4e87b1ac605bcb19'
if (!(Test-Path $reference)) {
    git clone --no-checkout https://github.com/mudassarzahid/gba-audio-tools.git $reference
    if ($LASTEXITCODE) {throw 'Offline music renderer clone failed'}
}
git -C $reference checkout --detach $revision
if ($LASTEXITCODE) {throw 'Pinned music renderer checkout failed'}
