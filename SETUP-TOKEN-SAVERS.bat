@echo off
rem FireRed Remaster - installs the token-saving tools from Eric Tech's video
rem "4 Free Repos That Cut Claude Code Token Usage":
rem   RTK       shrinks command output (git, rg, ls, builds ...) before Claude reads it
rem   Ponytail  Claude plugin that makes it write less, simpler code
rem   Graphify  a map of the code, so Claude looks things up instead of searching
rem Headroom (the 4th) is left out on purpose - see docs\TOKEN-SAVERS.md.
rem Close Claude Code first. Run once; running it again is harmless.
cd /d "%~dp0"
set "PS=powershell -NoProfile -ExecutionPolicy Bypass -Command"
set "FRESH=$env:Path=[Environment]::GetEnvironmentVariable('Path','Machine')+';'+[Environment]::GetEnvironmentVariable('Path','User')+';'+$env:USERPROFILE+'\.local\bin';"

echo.
echo === 1/4  Helpers: Node.js (Ponytail needs it) and uv (installs Graphify) ===
where node >nul 2>&1 || winget install --id OpenJS.NodeJS.LTS -e
where uv >nul 2>&1 || winget install --id astral-sh.uv -e

echo.
echo === 2/4  RTK ===
%PS% "%FRESH% if (-not (Get-Command rtk -ErrorAction SilentlyContinue)) { winget install --id rtk-ai.rtk -e }"
%PS% "%FRESH% rtk init -g --auto-patch"
%PS% "%FRESH% if (Get-Command codex -ErrorAction SilentlyContinue) { rtk init -g --codex --auto-patch }"

echo.
echo === 3/4  Ponytail (Claude Code plugin) ===
%PS% "%FRESH% claude plugin marketplace add DietrichGebert/ponytail; claude plugin install ponytail@ponytail"

echo.
echo === 4/4  Graphify (code map) ===
%PS% "%FRESH% uv tool install graphifyy"
%PS% "%FRESH% graphify install --project"
echo Building the code map (code only, runs on your PC, about a minute)...
%PS% "%FRESH% graphify extract ."
if errorlevel 1 echo The map didn't build here - in Claude Code, type:  /graphify .

echo.
echo Done. Open "FireRed Remaster - Claude Code" again to use them.
echo   rtk gain        - see how much RTK has saved
echo   /ponytail lite  - in Claude Code, gentler Ponytail (/ponytail off to pause it)
echo   docs\TOKEN-SAVERS.md has the details.
pause
