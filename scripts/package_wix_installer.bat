@echo off
setlocal

set INPUT=%~1
set OUTPUT=%~2

if "%INPUT%"=="" (
  echo usage:
  echo   package_wix_installer.bat ^<build-folder-or-exe^> [output-folder]
  exit /b 1
)

if exist "%INPUT%\can_monitor_qml_reboot.exe" (
  set BUILD_DIR=%INPUT%
) else if /I "%~x1"==".exe" (
  for %%I in ("%~f1") do set BUILD_DIR=%%~dpI
) else (
  echo build folder or exe not found: %INPUT%
  exit /b 1
)

if "%OUTPUT%"=="" set OUTPUT=%BUILD_DIR%\installer_release
if exist "%OUTPUT%" rmdir /s /q "%OUTPUT%"
mkdir "%OUTPUT%" || exit /b 1

where wix >nul 2>nul
if errorlevel 1 (
  echo WiX CLI not found on PATH.
  echo Portable packaging is still available through deploy_release.bat.
  echo This hook is reserved for the MSI stage of the production-readiness plan.
  exit /b 2
)

echo WiX CLI detected, but the project-specific .wxs authoring file is not committed yet.
echo Add packaging\wix\can_monitor_reboot.wxs and extend this script to produce the final MSI.
exit /b 3
