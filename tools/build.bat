@echo off
rem Build FireRed Remaster. The first build compiles the whole game (several
rem minutes); after that only changed files are rebuilt.
rem   tools\build.bat          normal build
rem   tools\build.bat -Clean   start from scratch
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" %*
set ERR=%ERRORLEVEL%
echo %cmdcmdline% | find /i "%~0" >nul && pause
exit /b %ERR%
