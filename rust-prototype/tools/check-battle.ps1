$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$exe=Join-Path $root 'output/FireRed Native 3D Prototype.exe'
$captures=Join-Path $root 'captures'
New-Item -ItemType Directory -Force $captures | Out-Null
foreach ($mode in @('smoke','diagonal-smoke','free-smoke','battle-smoke')) {
    $process=Start-Process -FilePath $exe -ArgumentList "--$mode" -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput (Join-Path $captures "$mode-battle-build.log") -RedirectStandardError (Join-Path $captures "$mode-battle-build-errors.log")
    if ($process.ExitCode) {throw "Native replay failed: $mode"}
    $name=if ($mode -eq 'battle-smoke') {'battle-smoke-result.json'} else {'smoke-result.json'}
    $report=Get-Content (Join-Path $root "output/$name") | ConvertFrom-Json
    if (!$report.passed) {throw "Native replay report failed: $mode"}
}
foreach ($view in @('battle','battle-turns','battle-win','battle-loss','settings')) {
    $path=Join-Path $captures "battle-build-$view.png"
    $argsList=@('--capture',('"'+$path+'"'),"--$view")
    $process=Start-Process -FilePath $exe -ArgumentList $argsList -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput (Join-Path $captures "$view-battle-build.log") -RedirectStandardError (Join-Path $captures "$view-battle-build-errors.log")
    if ($process.ExitCode -or !(Test-Path $path)) {throw "Capture failed: $view"}
}
Write-Output 'Four native input replays passed; five fresh battle/settings captures produced. Audio forced silent.'
