@echo off
REM ---------------------------------------------------------------------------
REM Opens the web flashing page properly on this computer, for trying it out
REM before putting it on GitHub.
REM
REM Double clicking docs\index.html does not work. A page opened straight off
REM the hard drive is not allowed to read the firmware files sitting next to it,
REM so installing fails with "Failed to download manifest". That is a browser
REM security rule and has nothing to do with the board.
REM
REM This serves the docs folder the way a real web site would, which is all the
REM browser was asking for. Close this window when you are finished.
REM ---------------------------------------------------------------------------

setlocal

cd /d "%~dp0docs" 2>nul
if errorlevel 1 (
  echo Could not find the docs folder next to this file.
  echo Keep preview.bat in the same folder as platformio.ini.
  echo.
  pause
  exit /b 1
)

REM Find a working Python. Actually running it weeds out the Microsoft Store
REM placeholder, which answers to the name and then does nothing.
set "PY="
for %%C in (python py python3) do (
  if not defined PY (
    %%C -c "import sys" >nul 2>&1 && set "PY=%%C"
  )
)
if not defined PY (
  if exist "%USERPROFILE%\.platformio\penv\Scripts\python.exe" (
    set "PY=%USERPROFILE%\.platformio\penv\Scripts\python.exe"
  )
)

if not defined PY (
  echo Could not find Python on this computer.
  echo.
  echo Python comes with PlatformIO, so opening this project in VS Code once
  echo will usually put it there. Failing that, install it from python.org.
  echo.
  pause
  exit /b 1
)

echo.
echo   Serving the flashing page at http://localhost:8000
echo   Opening it now. Close this window when you are finished.
echo.

start "" "http://localhost:8000/"
"%PY%" -m http.server 8000
