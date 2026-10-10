@echo off
rem Opens Claude Code in the FireRed Remaster project folder.
cd /d "%~dp0.."
title FireRed Remaster - Claude Code
where claude >nul 2>&1
if not errorlevel 1 (claude & goto end)
if exist "%USERPROFILE%\.local\bin\claude.exe" ("%USERPROFILE%\.local\bin\claude.exe" & goto end)
echo Claude Code isn't installed yet - run SETUP-CLAUDE-CODE.bat first.
pause
:end
