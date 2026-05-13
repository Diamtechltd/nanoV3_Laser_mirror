@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
cd /d "%SCRIPT_DIR%"
set "PLATFORMIO_CORE_DIR=%SCRIPT_DIR%.pio-core"

set "PLATFORMIO_CMD="
for /f "delims=" %%I in ('where pio 2^>nul') do (
  set "PLATFORMIO_CMD=%%I"
  goto :pio_found
)

set "PLATFORMIO_CMD=%USERPROFILE%\.platformio\penv\Scripts\platformio.exe"
if not exist "%PLATFORMIO_CMD%" (
  echo ERROR: PlatformIO was not found in PATH or at "%USERPROFILE%\.platformio\penv\Scripts\platformio.exe".
  exit /b 1
)

:pio_found
echo Building PlatformIO project...
call "%PLATFORMIO_CMD%" run
if errorlevel 1 (
  echo Build failed.
  exit /b 1
)

echo Build succeeded.
exit /b 0
