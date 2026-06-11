# BUILD_AND_VERIFY_KO

Qt/CMake build, test, deploy smoke 전용 runbook이다. 반복 작업은 [[../../.agents/skills/qt-build-verify/SKILL]]를 따른다.

## Environment
- Windows
- Qt 6.10.2
- MSVC2022 x64 개발 환경
- 로컬 Qt 경로는 `CMakeUserPresets.json` 또는 Codex local environment override가 담당한다.

## Release
```powershell
cmake --preset local-vs-release-qt6
cmake --build --preset build-release-local
ctest --test-dir out/build/x64-Release --output-on-failure
```

## Debug
```powershell
cmake --preset local-vs-debug-qt6
cmake --build --preset build-debug-local
ctest --test-dir out/build/x64-Debug --output-on-failure
```

## Portable Deploy Smoke
```powershell
scripts\deploy_release.bat out\build\x64-Release out\build\x64-Release\portable_typed_check
```

실행 smoke는 release exe 또는 portable exe를 짧게 기동해 process 생존과 startup log를 확인한다.

## Known Notes
- `QTP0004` CMake dev warning은 이전 기준에서 build/test blocker가 아니었다.
- 검증을 실행하지 않았으면 성공이라고 보고하지 않는다.
