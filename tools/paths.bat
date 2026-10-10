@echo off
rem Where the game lives. Change GAME_DIR if you move the game folder.
set "GAME_DIR=%USERPROFILE%\Desktop\FireRedRecomp-esc-menu"
set "GAME_EXE=FireRed Remaster.exe"
if not exist "%GAME_DIR%\FireRed Remaster.exe" if exist "%GAME_DIR%\Pokemon Fire Red.exe" set "GAME_EXE=Pokemon Fire Red.exe"
