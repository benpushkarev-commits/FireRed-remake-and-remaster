$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$exe=Join-Path $root 'output/FireRed Native 3D Prototype.exe'
$captures=Join-Path $root 'captures'
New-Item -ItemType Directory -Force $captures | Out-Null
Push-Location (Join-Path $root 'output')
try {
    $process=Start-Process -FilePath $exe -ArgumentList '--smoke' -WindowStyle Hidden -PassThru -Wait -RedirectStandardOutput (Join-Path $captures 'smoke.log') -RedirectStandardError (Join-Path $captures 'smoke-errors.log')
    if ($process.ExitCode) {throw 'Native input replay failed; inspect smoke-result.json'}
    $report=Get-Content smoke-result.json | ConvertFrom-Json
    if (!$report.passed) {throw 'Native replay did not pass'}
    $process=Start-Process -FilePath $exe -ArgumentList '--diagonal-smoke' -WindowStyle Hidden -PassThru -Wait -RedirectStandardOutput (Join-Path $captures 'diagonal-smoke.log') -RedirectStandardError (Join-Path $captures 'diagonal-smoke-errors.log')
    if ($process.ExitCode) {throw 'Native diagonal input replay failed'}
    $diagonalReport=Get-Content smoke-result.json | ConvertFrom-Json
    if (!$diagonalReport.passed) {throw 'Native diagonal replay did not pass'}
    Copy-Item smoke-result.json diagonal-smoke-result.json
    $process=Start-Process -FilePath $exe -ArgumentList '--free-smoke' -WindowStyle Hidden -PassThru -Wait -RedirectStandardOutput (Join-Path $captures 'free-smoke.log') -RedirectStandardError (Join-Path $captures 'free-smoke-errors.log')
    if ($process.ExitCode) {throw 'Native free movement replay failed'}
    $freeReport=Get-Content smoke-result.json | ConvertFrom-Json
    if (!$freeReport.passed -or !$freeReport.arbitrary_heading_verified -or !$freeReport.free_movement) {throw 'Free movement replay did not verify continuous controls'}
    Copy-Item smoke-result.json free-smoke-result.json
    foreach ($view in @('pallet','free','diagonal','route','home','orbit','dialogue','settings')) {
        $path=Join-Path $captures "$view.png"
        $argsList=@('--capture',('"'+$path+'"'))
        if ($view -eq 'free') {$argsList+='--free-view'}
        if ($view -eq 'diagonal') {$argsList+='--diagonal-view'}
        if ($view -eq 'route') {$argsList+='--route'}
        if ($view -eq 'home') {$argsList+='--interior'}
        if ($view -eq 'orbit') {$argsList+='--orbit'}
        if ($view -eq 'dialogue') {$argsList+='--dialogue'}
        if ($view -eq 'settings') {$argsList+='--settings'}
        $process=Start-Process -FilePath $exe -ArgumentList $argsList -WindowStyle Hidden -PassThru -Wait -RedirectStandardOutput (Join-Path $captures "$view.log") -RedirectStandardError (Join-Path $captures "$view-errors.log")
        if ($process.ExitCode -or !(Test-Path $path)) {throw "Capture failed: $view"}
    }
    Write-Output "Native replay passed ($($report.moves) moves, $($report.dialogues) dialogues). Diagonal replay passed ($($diagonalReport.moves) moves). Free movement replay passed ($($freeReport.moves) moves). Fresh captures in $captures"
} finally {Pop-Location}

