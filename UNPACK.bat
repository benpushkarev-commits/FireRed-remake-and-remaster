@echo off
rem FireRed Remaster project - unpacks the full source into this folder (run once).
cd /d "%~dp0"
if not exist "_transfer\project.zip.part0" (echo Nothing to unpack - already done. & pause & exit /b 0)
echo Joining the parts...
copy /b "_transfer\project.zip.part0"+"_transfer\project.zip.part1"+"_transfer\project.zip.part2"+"_transfer\project.zip.part3"+"_transfer\project.zip.part4" "_transfer\project.zip" >nul
echo Checking the download...
certutil -hashfile "_transfer\project.zip" SHA1 | find /i "fd1e82b488c547f9888336bd467aa72a8735e578" >nul
if errorlevel 1 (echo The parts don't match - nothing was unpacked. Ask Claude to send them again. & del "_transfer\project.zip" >nul 2>&1 & pause & exit /b 1)
echo Unpacking about 550 MB - this can take a minute...
tar -xf "_transfer\project.zip"
if not exist "AGENTS.md" (echo Something went wrong - nothing was unpacked. & pause & exit /b 1)
rmdir /s /q "_transfer"
echo.
echo Done. Open START-HERE.txt to see what's next.
pause
