# 빌드 폴더 사용법 (가장 직관적인 기준)

이 프로젝트에서 **실행 확인용 기준 폴더**는 아래다.

```text
프로젝트루트\outuildd-Release
```

여기 안에 있는 핵심 파일은 보통 아래 4개다.

```text
can_monitor_qml_reboot.exe
run_release_here.bat
deploy_release.bat
README_SETUP_KO.md
```

## 1. 패치/통합본을 받았을 때
1. 프로젝트 루트에 압축을 푼다.
2. 기존 파일 덮어쓰기를 허용한다.
3. CMake 캐시 삭제.
4. Configure / Generate.
5. 모두 다시 빌드.

## 2. 빌드 직후 가장 먼저 할 것
빌드가 끝나면 아래 폴더로 간다.

```text
outuildd-Release
```

여기서 먼저 실행 확인:

```bat
run_release_here.bat
```

또는

```bat
can_monitor_qml_reboot.exe
```

## 3. 다른 PC 전달용 폴더 만들기
같은 폴더에서 아래 둘 중 하나:

```bat
deploy_release.bat
```

또는

```bat
deploy_release.bat .
```

성공하면 아래 폴더가 생긴다.

```text
outuildd-Release\deploy_release
```

그 안에서 최종 실행 확인:

```bat
run_release_here.bat
```

## 4. 다음에 GPT에게 줄 때 가장 좋은 방식
### 코드/구조 수정이 목적일 때
아래만 주면 충분하다.

```text
src/
qml/
data/
scripts/
CMakeLists.txt
CMakeUserPresets.json
BRIEF.md
README_SETUP_KO.md
BUILD_FOLDER_USAGE_KO.md
```

### 실행/배포 문제가 목적일 때
아래 폴더만 ZIP으로 주면 된다.

```text
outuildd-Release
```

## 5. 이번 통합본의 구성
- 프로젝트 소스는 유지
- `.vs`, `x64-Debug`, CMake 중간 산출물 제거
- `x64-Release`는 **실행에 필요한 파일 위주**로만 정리
- 모델/룰 JSON은 `data/` 폴더로 정리
