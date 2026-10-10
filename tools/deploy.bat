@echo off
rem Copy the freshly built game into the game folder (close the game first).
rem Keeps whatever name the exe has there and backs up the previous one.
call "%~dp0paths.bat"
set "NEW=%~dp0..\build\FireRed Remaster.exe"
set ERR=0
if not exist "%NEW%" (echo No build found - run tools\build.bat first. & set "ERR=1" & goto done)
if not exist "%GAME_DIR%" (echo Game folder not found: %GAME_DIR% - edit tools\paths.bat. & set "ERR=1" & goto done)
if not exist "%GAME_DIR%\install_files\old" mkdir "%GAME_DIR%\install_files\old"
if exist "%GAME_DIR%\%GAME_EXE%" copy /y "%GAME_DIR%\%GAME_EXE%" "%GAME_DIR%\install_files\old\FireRed Remaster (previous).exe" >nul
copy /y "%NEW%" "%GAME_DIR%\%GAME_EXE%" >nul
if errorlevel 1 (echo Copy failed - is the game still running? & set "ERR=1" & goto done)
echo Deployed: "%GAME_DIR%\%GAME_EXE%"
:done
echo %cmdcmdline% | find /i "%~0" >nul && pause
exit /b %ERR%
