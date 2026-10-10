$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$reference=Join-Path $root 'reference/pokefirered'
$revision='037335f4c725d7c9aecdac87066f2002b4bd7e14'
if (!(Test-Path $reference)) {
    git clone --filter=blob:none --no-checkout --sparse https://github.com/pret/pokefirered.git $reference
    if ($LASTEXITCODE) {throw 'Reference clone failed'}
}
$paths=@('src','include')
foreach ($map in @('PalletTown','Route1','PalletTown_PlayersHouse_1F','PalletTown_PlayersHouse_2F','PalletTown_RivalsHouse','PalletTown_ProfessorOaksLab')) {
    $paths+="data/maps/$map"
    $paths+="data/layouts/$map"
}
foreach ($tileset in @('primary/general','primary/building','secondary/pallet_town','secondary/generic_building_1','secondary/generic_building_2','secondary/lab')) {
    $paths+="data/tilesets/$tileset"
}
git -C $reference sparse-checkout set $paths
if ($LASTEXITCODE) {throw 'Sparse checkout failed'}
git -C $reference checkout --detach $revision
if ($LASTEXITCODE) {throw 'Pinned reference checkout failed'}
