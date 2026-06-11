@echo off
setlocal ENABLEDELAYEDEXPANSION

set INPUT=%~1
set OUTPUT=%~2
set EXE=
set BUILD_DIR=

if "%INPUT%"=="" (
  if exist "%~dp0can_monitor_qml_reboot.exe" (
    set INPUT=%~dp0
  ) else (
    echo 사용법:
    echo   deploy_release.bat
    echo   deploy_release.bat .
    echo   deploy_release.bat "C:\path\to\can_monitor_qml_reboot.exe"
    echo   deploy_release.bat "C:\path\to\build\folder"
    echo   deploy_release.bat "C:\path\to\build\folder" "C:\path\to\deploy-folder"
    exit /b 1
  )
)

if exist "%INPUT%\can_monitor_qml_reboot.exe" (
  set BUILD_DIR=%INPUT%
  set EXE=%INPUT%\can_monitor_qml_reboot.exe
) else if /I "%~x1"==".exe" (
  set EXE=%~f1
  for %%I in ("%EXE%") do set BUILD_DIR=%%~dpI
) else (
  echo 대상 exe 또는 빌드 폴더를 찾지 못했습니다: %INPUT%
  exit /b 1
)

if "!BUILD_DIR:~-1!"=="\" set BUILD_DIR=!BUILD_DIR:~0,-1!
if not exist "%EXE%" (
  echo exe를 찾지 못했습니다: %EXE%
  exit /b 1
)

if "%OUTPUT%"=="" set OUTPUT=%BUILD_DIR%\deploy_release
for %%I in ("%OUTPUT%") do set OUTPUT=%%~fI

set WINDEPLOYQT=
for /f "delims=" %%I in ('where windeployqt 2^>nul') do (
  if not defined WINDEPLOYQT set WINDEPLOYQT=%%~fI
)

if not defined WINDEPLOYQT if defined CAN_MONITOR_QT_PREFIX_PATH (
  set WINDEPLOYQT=%CAN_MONITOR_QT_PREFIX_PATH%\bin\windeployqt.exe
) else if defined QTDIR (
  set WINDEPLOYQT=%QTDIR%\bin\windeployqt.exe
) else (
  set WINDEPLOYQT=C:\Qt\6.10.2\msvc2022_64\bin\windeployqt.exe
)

if not exist "%WINDEPLOYQT%" (
  echo windeployqt를 찾지 못했습니다: %WINDEPLOYQT%
  exit /b 1
)

set PROJECT_DIR=
for %%P in (
  "%~dp0.."
  "%BUILD_DIR%"
  "%BUILD_DIR%\.."
  "%BUILD_DIR%\..\.."
  "%BUILD_DIR%\..\..\.."
  "%BUILD_DIR%\..\..\..\.."
) do (
  if not defined PROJECT_DIR (
    if exist "%%~fP\qml\Main.qml" set PROJECT_DIR=%%~fP
  )
)

if not defined PROJECT_DIR (
  echo qml\Main.qml 을 포함한 프로젝트 루트를 찾지 못했습니다.
  echo 권장 위치: 프로젝트루트\out\build\x64-Release 안에서 deploy_release.bat 실행
  exit /b 1
)

echo.
echo [1/5] 출력 폴더 준비: %OUTPUT%
if exist "%OUTPUT%" rmdir /s /q "%OUTPUT%"
mkdir "%OUTPUT%" || exit /b 1

echo [2/5] 실행 파일/기본 문서 복사
copy /y "%EXE%" "%OUTPUT%\can_monitor_qml_reboot.exe" >nul || exit /b 1
if exist "%BUILD_DIR%\can_monitor_qml_reboot.pdb" copy /y "%BUILD_DIR%\can_monitor_qml_reboot.pdb" "%OUTPUT%\" >nul
if exist "%BUILD_DIR%\README_SETUP_KO.md" copy /y "%BUILD_DIR%\README_SETUP_KO.md" "%OUTPUT%\" >nul
if exist "%BUILD_DIR%\BUILD_FOLDER_USAGE_KO.md" copy /y "%BUILD_DIR%\BUILD_FOLDER_USAGE_KO.md" "%OUTPUT%\" >nul
if exist "%PROJECT_DIR%\scripts\run_release_here.bat" (
  copy /y "%PROJECT_DIR%\scripts\run_release_here.bat" "%OUTPUT%\" >nul
) else if exist "%BUILD_DIR%\run_release_here.bat" (
  copy /y "%BUILD_DIR%\run_release_here.bat" "%OUTPUT%\" >nul
)
if exist "%PROJECT_DIR%\scripts\deploy_release.bat" (
  copy /y "%PROJECT_DIR%\scripts\deploy_release.bat" "%OUTPUT%\" >nul
) else if exist "%BUILD_DIR%\deploy_release.bat" (
  copy /y "%BUILD_DIR%\deploy_release.bat" "%OUTPUT%\" >nul
)
if exist "%BUILD_DIR%\package_wix_installer.bat" copy /y "%BUILD_DIR%\package_wix_installer.bat" "%OUTPUT%\" >nul
if exist "%BUILD_DIR%\hil_control_smoke.py" copy /y "%BUILD_DIR%\hil_control_smoke.py" "%OUTPUT%\" >nul
if exist "%BUILD_DIR%\hil_vsm_user_route_stress.py" copy /y "%BUILD_DIR%\hil_vsm_user_route_stress.py" "%OUTPUT%\" >nul
if exist "%BUILD_DIR%\analyze_typed_capture.py" copy /y "%BUILD_DIR%\analyze_typed_capture.py" "%OUTPUT%\" >nul
if exist "%BUILD_DIR%\field_latest_capture_report.py" copy /y "%BUILD_DIR%\field_latest_capture_report.py" "%OUTPUT%\" >nul
if exist "%PROJECT_DIR%\scripts\make_release_manifest.py" (
  copy /y "%PROJECT_DIR%\scripts\make_release_manifest.py" "%OUTPUT%\" >nul
) else if exist "%BUILD_DIR%\make_release_manifest.py" (
  copy /y "%BUILD_DIR%\make_release_manifest.py" "%OUTPUT%\" >nul
)

echo [3/5] Qt 런타임 배치
"%WINDEPLOYQT%" --qmldir "%PROJECT_DIR%\qml" --no-translations --compiler-runtime "%OUTPUT%\can_monitor_qml_reboot.exe" || exit /b 1

echo [4/5] 데이터/문서 복사
if not exist "%OUTPUT%\data" mkdir "%OUTPUT%\data"
if not exist "%OUTPUT%\packaging" mkdir "%OUTPUT%\packaging"
if not exist "%OUTPUT%\docs" mkdir "%OUTPUT%\docs"
if not exist "%OUTPUT%\docs\runbooks" mkdir "%OUTPUT%\docs\runbooks"
if exist "%BUILD_DIR%\data\final_vms_model_R13_rev2.json" (
  copy /y "%BUILD_DIR%\data\final_vms_model_R13_rev2.json" "%OUTPUT%\data\" >nul
) else if exist "%PROJECT_DIR%\data\final_vms_model_R13_rev2.json" (
  copy /y "%PROJECT_DIR%\data\final_vms_model_R13_rev2.json" "%OUTPUT%\data\" >nul
)
if exist "%BUILD_DIR%\data\vms_rules_MERGED_R13.json" (
  copy /y "%BUILD_DIR%\data\vms_rules_MERGED_R13.json" "%OUTPUT%\data\" >nul
) else if exist "%PROJECT_DIR%\data\vms_rules_MERGED_R13.json" (
  copy /y "%PROJECT_DIR%\data\vms_rules_MERGED_R13.json" "%OUTPUT%\data\" >nul
)
if exist "%BUILD_DIR%\data\vms_model_turn77_system_drive_merged_realcan_refresh2_final.json" (
  copy /y "%BUILD_DIR%\data\vms_model_turn77_system_drive_merged_realcan_refresh2_final.json" "%OUTPUT%\data\" >nul
) else if exist "%PROJECT_DIR%\data\vms_model_turn77_system_drive_merged_realcan_refresh2_final.json" (
  copy /y "%PROJECT_DIR%\data\vms_model_turn77_system_drive_merged_realcan_refresh2_final.json" "%OUTPUT%\data\" >nul
)
if exist "%BUILD_DIR%\packaging\SBOM.spdx.json" (
  copy /y "%BUILD_DIR%\packaging\SBOM.spdx.json" "%OUTPUT%\packaging\" >nul
) else if exist "%PROJECT_DIR%\packaging\SBOM.spdx.json" (
  copy /y "%PROJECT_DIR%\packaging\SBOM.spdx.json" "%OUTPUT%\packaging\" >nul
)
if exist "%BUILD_DIR%\packaging\THIRD_PARTY_NOTICES.txt" (
  copy /y "%BUILD_DIR%\packaging\THIRD_PARTY_NOTICES.txt" "%OUTPUT%\packaging\" >nul
) else if exist "%PROJECT_DIR%\packaging\THIRD_PARTY_NOTICES.txt" (
  copy /y "%PROJECT_DIR%\packaging\THIRD_PARTY_NOTICES.txt" "%OUTPUT%\packaging\" >nul
)
if exist "%PROJECT_DIR%\docs\runbooks\FIELD_VALIDATION_KO.md" (
  copy /y "%PROJECT_DIR%\docs\runbooks\FIELD_VALIDATION_KO.md" "%OUTPUT%\docs\runbooks\" >nul
)

echo [5/5] helper and manifest
if not exist "%PROJECT_DIR%\scripts\run_release_here.bat" echo Missing run_release_here.bat.& exit /b 1
copy /y "%PROJECT_DIR%\scripts\run_release_here.bat" "%OUTPUT%\run_release_here.bat" >nul
if errorlevel 1 exit /b 1
if not exist "%OUTPUT%\make_release_manifest.py" echo Missing make_release_manifest.py.& exit /b 1
py -3 "%OUTPUT%\make_release_manifest.py" --package-dir "%OUTPUT%" --project-dir "%PROJECT_DIR%" --build-dir "%BUILD_DIR%" --baseline-id "workspace-direct-edit"
if errorlevel 1 exit /b 1

echo.
echo Done: %OUTPUT%
echo Run: run_release_here.bat
endlocal
exit /b 0
