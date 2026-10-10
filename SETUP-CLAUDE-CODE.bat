@echo off
rem FireRed Remaster - sets up Claude Code for this project (run once).
rem  1. installs Git for Windows if it's missing (Claude Code uses its Bash)
rem  2. installs Claude Code (official installer from claude.ai, auto-updates)
rem  3. puts a "FireRed Remaster - Claude Code" shortcut on your Desktop
cd /d "%~dp0"
echo.
where git >nul 2>&1
if errorlevel 1 (
  echo === Installing Git for Windows ===
  winget install --id Git.Git -e
)
set "CLAUDE_EXE=%USERPROFILE%\.local\bin\claude.exe"
where claude >nul 2>&1
if errorlevel 1 if not exist "%CLAUDE_EXE%" (
  echo === Installing Claude Code ===
  powershell -NoProfile -ExecutionPolicy Bypass -Command "irm https://claude.ai/install.ps1 | iex"
)
where claude >nul 2>&1
if errorlevel 1 if not exist "%CLAUDE_EXE%" (
  echo.
  echo Claude Code did not install. Check your internet connection and run this again.
  pause
  exit /b 1
)
echo === Adding Desktop shortcuts ===
powershell -NoProfile -Command "$d=[Environment]::GetFolderPath('Desktop'); $w=New-Object -ComObject WScript.Shell; $s=$w.CreateShortcut($d+'\FireRed Remaster - Claude Code.lnk'); $s.TargetPath='%~dp0tools\claude-here.bat'; $s.WorkingDirectory='%~dp0'; $s.Save()"
where codex >nul 2>&1
if not errorlevel 1 powershell -NoProfile -Command "$d=[Environment]::GetFolderPath('Desktop'); $w=New-Object -ComObject WScript.Shell; $s=$w.CreateShortcut($d+'\FireRed Remaster - Codex.lnk'); $s.TargetPath='%~dp0tools\codex-here.bat'; $s.WorkingDirectory='%~dp0'; $s.Save()"
echo.
echo All set. Open "FireRed Remaster - Claude Code" on your Desktop.
echo The first time it opens, log in with your Claude account in the browser.
pause
