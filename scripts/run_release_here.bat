@echo off
setlocal
pushd "%~dp0"
if not exist can_monitor_qml_reboot.exe (
  echo can_monitor_qml_reboot.exe 를 찾지 못했습니다.
  popd
  exit /b 1
)
can_monitor_qml_reboot.exe > run_release.log 2>&1
set EXITCODE=%ERRORLEVEL%
popd
endlocal & exit /b %EXITCODE%
