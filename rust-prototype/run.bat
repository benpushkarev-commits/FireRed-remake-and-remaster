@echo off
cd /d "%~dp0output"
if exist "FireRed Native 3D Prototype.pending.exe" (
    copy /Y "FireRed Native 3D Prototype.pending.exe" "FireRed Native 3D Prototype.exe" >nul 2>nul
    if errorlevel 1 (
        echo Close the Rust game, then run this file again to apply the update.
        pause
        exit /b 1
    )
    del "FireRed Native 3D Prototype.pending.exe"
)
start "" "FireRed Native 3D Prototype.exe"

