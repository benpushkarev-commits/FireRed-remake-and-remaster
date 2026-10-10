@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\build.ps1" %*

