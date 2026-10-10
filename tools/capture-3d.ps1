param([string]$Name = 'view', [string]$Replay = 'view.csv', [int]$Frame = 2100, [switch]$Resume,
      [string]$Save = 'check.sav', [string]$State = 'check.state1', [int]$Scale = 4,
      [int]$ViewWidth = 284)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$test = Join-Path $root 'tools/check-game'
$env:SDL_VIDEODRIVER = 'dummy'
$env:SDL_RENDER_DRIVER = 'software'
$env:SDL_AUDIODRIVER = 'dummy'
$env:GBARECOMP_INPUT_REPLAY = Join-Path $test $Replay
$env:GBARECOMP_RENDER_CAPTURE = Join-Path $test "$Name.bmp"
$env:GBARECOMP_RENDER_CAPTURE_FRAME = "$Frame"
$extra = @()
if ($Resume) { $extra = @('--load-state', $State) }
$captureProc = Start-Process -FilePath (Join-Path $test 'FireRedCheck.exe') `
    -ArgumentList (@('--window', '--scale', "$Scale", '--frames', "$($Frame + 20)", '--volume', '0',
        '--rom', 'check.gba', '--save', $Save, '--bios',
        '"C:/Users/benpu/Desktop/FireRedRecomp-esc-menu/gba_bios.bin"', '--view-width', "$ViewWidth") + $extra) `
    -WorkingDirectory $test -WindowStyle Hidden `
    -RedirectStandardError (Join-Path $test "$Name.err") `
    -RedirectStandardOutput (Join-Path $test "$Name.out") -PassThru
$captureProc.WaitForExit()
if (!(Test-Path $env:GBARECOMP_RENDER_CAPTURE)) { throw 'Capture file missing' }
Write-Host "Captured: $env:GBARECOMP_RENDER_CAPTURE"
