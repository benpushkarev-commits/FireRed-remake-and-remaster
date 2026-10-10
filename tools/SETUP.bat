@echo off
rem FireRed Remaster - one-time setup of the free build tools.
rem Installs (with winget, built into Windows 10/11):
rem   MSYS2    - the C++ compiler, CMake and Ninja used to build the game
rem   Git      - local history and diffs only (nothing is ever uploaded)
rem   ripgrep  - fast code search that AI tools use instead of reading files
rem   Python   - for the small helper scripts in tools\py
rem Windows may ask you to confirm each install.
echo.
echo === Installing MSYS2, Git, ripgrep and Python ===
winget install --id MSYS2.MSYS2 -e
winget install --id Git.Git -e
winget install --id BurntSushi.ripgrep.MSVC -e
winget install --id Python.Python.3.12 -e
if not exist "C:\msys64\usr\bin\bash.exe" (
  echo.
  echo MSYS2 was not found in C:\msys64. Install it, then run SETUP.bat again.
  pause
  exit /b 1
)
echo.
echo === Updating MSYS2 and installing the compiler packages ===
"C:\msys64\usr\bin\bash.exe" -lc "pacman -Syu --noconfirm"
"C:\msys64\usr\bin\bash.exe" -lc "pacman -Syu --noconfirm"
"C:\msys64\usr\bin\bash.exe" -lc "pacman -S --needed --noconfirm mingw-w64-x86_64-gcc mingw-w64-x86_64-binutils mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-ccache"
if not exist "C:\msys64\mingw64\bin\c++.exe" (
  echo.
  echo The compiler did not install. Run SETUP.bat again.
  pause
  exit /b 1
)
echo.
echo Setup finished. Build the game with tools\build.bat
pause
