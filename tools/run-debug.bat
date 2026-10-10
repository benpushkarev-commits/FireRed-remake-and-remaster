@echo off
rem Start the game with the debug port open (127.0.0.1:19842) so tools can
rem read its memory (tools\py\tcp.py). Playing normally doesn't need this.
call "%~dp0paths.bat"
cd /d "%GAME_DIR%"
start "" "%GAME_EXE%" --tcp-observe 19842
