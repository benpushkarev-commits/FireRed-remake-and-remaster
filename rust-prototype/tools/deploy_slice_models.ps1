param([string]$Destination='C:\Users\benpu\Desktop\build 1 pokemon fire')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$destinationPath=(Resolve-Path -LiteralPath $Destination).Path
$target=Join-Path $destinationPath 'FireRed Native 3D Prototype 0.1.exe'
$source=Join-Path $root 'output\FireRed Native 3D Prototype.exe'
if (!(Test-Path -LiteralPath $target)) {throw 'Expected playable-slice executable is missing'}
foreach ($process in Get-Process) {
    if ($process.Path -eq $target) {throw 'Close the playable slice before deploying'}
}
$backup=Join-Path $destinationPath ('backups\before-model-animations-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $backup -Force | Out-Null
foreach ($name in @('FireRed Native 3D Prototype 0.1.exe','config.ini','native-state.json')) {
    $path=Join-Path $destinationPath $name
    if (Test-Path -LiteralPath $path) {Copy-Item -LiteralPath $path -Destination $backup}
}
$oldRed=Join-Path $destinationPath 'assets\models\red\red.glb'
if (Test-Path -LiteralPath $oldRed) {Copy-Item -LiteralPath $oldRed -Destination (Join-Path $backup 'red.glb')}
$state=Join-Path $destinationPath 'native-state.json'
$before=(Get-FileHash -LiteralPath $state -Algorithm SHA256).Hash
$modelTarget=Join-Path $destinationPath 'assets\models'
New-Item -ItemType Directory -Path $modelTarget -Force | Out-Null
foreach ($item in Get-ChildItem -LiteralPath (Join-Path $root 'output\assets\models')) {
    Copy-Item -LiteralPath $item.FullName -Destination $modelTarget -Recurse -Force
}
Copy-Item -LiteralPath $source -Destination $target -Force
$config=Join-Path $destinationPath 'config.ini'
$text=Get-Content -LiteralPath $config -Raw
foreach ($key in @('character_animation','trainer_models','pokemon_models')) {
    if ($text -notmatch "(?m)^$key=") {Add-Content -LiteralPath $config -Value "$key=1" -Encoding UTF8}
}
$after=(Get-FileHash -LiteralPath $state -Algorithm SHA256).Hash
if ($before -ne $after) {throw 'Native save changed during deployment'}
$sourceHash=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
$installedHash=(Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
if ($sourceHash -ne $installedHash) {throw 'Installed executable hash mismatch'}
foreach ($model in Get-ChildItem -LiteralPath (Join-Path $root 'output\assets\models') -Filter '*.glb' -Recurse) {
    $relative=$model.FullName.Substring((Join-Path $root 'output\assets\models').Length).TrimStart('\')
    $installed=Join-Path $modelTarget $relative
    if ((Get-FileHash -LiteralPath $model.FullName).Hash -ne (Get-FileHash -LiteralPath $installed).Hash) {throw "Installed model hash mismatch: $relative"}
}
$report=@{executable=$target;sha256=$installedHash;save_unchanged=$true;save_sha256=$after;backup=$backup}
$report | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $root 'tmp\slice-models-deploy.json') -Encoding UTF8
Write-Output 'Playable slice updated; executable and model hashes match; native save unchanged.'
