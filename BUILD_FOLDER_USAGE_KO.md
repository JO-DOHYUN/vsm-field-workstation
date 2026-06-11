# BUILD_FOLDER_USAGE_KO

이 파일은 CMake install/deploy 단계에서 실행 폴더로 복사되는 호환 안내 파일이다.

현재 폴더 사용 기준은 `docs/PROJECT_FOLDER_GUIDE_KO.md`를 따른다. 과거 build folder 안내 전문은 `history/changes/legacy-build-folder-usage-KO.md`에 보관한다.

## 현재 기준

- `out/`: CMake build output
- `replay_data/`: runtime log/capture/snapshot data root
- `.logs/`: local Codex/build/run logs
- `src/`, `qml/`, `tests/`, `shared/`: source/test 영역

`out/`, `.logs/`, 실제 `replay_data` 산출물은 git source가 아니다.
