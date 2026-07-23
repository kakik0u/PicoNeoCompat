@echo off
setlocal
echo PICO Neo 2 NVENC compatibility shim installer
echo.
echo SteamVR must be completely closed before continuing.
echo A Windows administrator prompt will appear.
echo.
pause
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$p = Start-Process powershell.exe -Verb RunAs -Wait -PassThru -ArgumentList '-NoProfile -ExecutionPolicy Bypass -File ""%~dp0scripts\install.ps1"" -Force'; exit $p.ExitCode"
if errorlevel 1 (
  echo.
  echo Installation did not complete. See the message above or README.md.
) else (
  echo.
  echo Installer finished. You can now start SteamVR.
)
pause
