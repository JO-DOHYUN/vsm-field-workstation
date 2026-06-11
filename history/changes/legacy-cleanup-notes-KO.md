# 정리 메모

## 내가 받은 ZIP 상태
- 프로젝트 루트와 빌드 산출물이 한 ZIP에 같이 들어 있었음
- `.vs`, `x64-Debug`, `x64-Release` 내부 CMake/Ninja/autogen 산출물까지 포함되어 있었음
- 실행에는 필요 없는 중복/중간 파일이 많았음

## 이번 통합본에서 제거한 것
- `.vs/`
- `src/backend/.vs/`
- `out/build/x64-Debug/`
- `out/build/x64-Release` 내부의 `.cmake`, `.qt`, `.rcc`, `CMakeFiles`, `autogen`, `Testing`, `build.ninja`, `CMakeCache.txt` 등
- 루트 중복 `final_vms_model_R13_rev2.json`

## 남긴 것
- 실제 소스 (`src`, `qml`, `data`, `scripts`)
- 문서 (`README_SETUP_KO.md`, `BUILD_FOLDER_USAGE_KO.md`, `BRIEF.md`)
- 실행 확인용 `out/build/x64-Release`
