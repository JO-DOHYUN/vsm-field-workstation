# CAN Monitor Reboot - 셋업 / 실행 / 배포 가이드

이 프로젝트는 **소스 루트**와 **실행 확인용 빌드 폴더**를 분리해서 보면 가장 헷갈리지 않는다.

## 0. 가장 중요한 폴더 2개

### 소스 수정 기준
```text
프로젝트루트
```
여기서 `src`, `qml`, `data`, `scripts`, `CMakeLists.txt`를 수정한다.

### 실행 확인 기준
```text
프로젝트루트\outuildd-Release
```
빌드가 끝나면 여기에서 실행 확인한다.

---

## 1. 처음부터 다시 빌드하는 가장 쉬운 순서
1. 프로젝트 루트에 통합본 압축을 푼다.
2. 기존 파일은 덮어쓴다.
3. Visual Studio에서 **CMake 캐시 삭제**.
4. 다시 Configure / Generate.
5. **모두 다시 빌드**.
6. 빌드가 끝나면 `out\build\x64-Release` 폴더로 간다.
7. `run_release_here.bat`를 먼저 실행한다.

---

## 2. 빌드 후 어디를 봐야 하는가
빌드 직후 기준 폴더:

```text
outuildd-Release
```

여기 안에 아래 파일이 있으면 정상 흐름이다.

```text
can_monitor_qml_reboot.exe
run_release_here.bat
deploy_release.bat
README_SETUP_KO.md
BUILD_FOLDER_USAGE_KO.md
datainal_vms_model_R13_rev2.json
datams_rules_MERGED_R13.json
```

### 가장 먼저 확인할 실행 방법
```bat
run_release_here.bat
```

### 직접 실행
```bat
can_monitor_qml_reboot.exe
```

VS 초록 화살표가 애매하면, **먼저 빌드 폴더 직접 실행 기준으로 판단**하면 된다.

---

## 3. 다른 PC 전달용 폴더 만들기
빌드 폴더(`out\build\x64-Release`)에서 아래 둘 중 하나 실행:

```bat
deploy_release.bat
```

또는

```bat
deploy_release.bat .
```

그러면 아래 폴더가 생긴다.

```text
outuildd-Release\deploy_release
```

이 폴더 안에서 최종 확인:

```bat
run_release_here.bat
```

---

## 4. 지금 내가 준 통합본의 구조
### 포함
- `src/`, `qml/`, `data/`, `scripts/`
- 문서 (`README_SETUP_KO.md`, `BUILD_FOLDER_USAGE_KO.md`, `BRIEF.md` 등)
- `out/build/x64-Release` 실행 확인용 폴더

### 제거
- `.vs/`
- `out/build/x64-Debug/`
- `x64-Release` 안의 CMake/Ninja/autogen 중간 산출물
- 루트 중복 모델 JSON

즉, **소스 보관용 + 실행 확인용 Release 폴더**만 남긴 통합본이다.

---

## 5. 다음에 GPT에게 어떤 ZIP을 주면 좋은가
### 코드 수정이 목적
아래만 ZIP:
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

### 실행/배포 문제 확인이 목적
아래만 ZIP:
```text
outuildd-Release
```

### 둘 다 같이 봐야 할 때
이번처럼 **통합본 전체 ZIP**을 주면 된다.

---

## 6. 현재 프로젝트 구현 범위
- 20바이트 CAN 패킷 수신 / 파싱
- 라이브 표시
- 로그 시작 / 중지 / 저장
- replay 로드 / seek
- timing / value / alarm / overview 구조
- 값해석 split 성능 완화, 로그 시작 부하 완화, replay 표시 일관성 보강

---

## 7. 최종 확인 체크
1. 빌드 폴더에서 `run_release_here.bat` 실행
2. live 연결 / 해제
3. 로그 시작 / 중지 / 저장
4. 값해석 split 열었을 때 반응성
5. replay 로드 / seek
6. `deploy_release.bat` 실행
7. `deploy_release` 폴더에서 다시 실행
