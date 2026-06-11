# AGENTS.md

이 저장소의 Codex 운영 규칙이다.
목표는 **실차에서 신뢰 가능한 CAN monitor / logger / replay / decode tool**을 회귀 없이 완성하는 것이다.

이 파일은 짧은 **운영 맵**으로 유지한다.
상세 작업 절차는 `.agents/skills/main-code-progress/SKILL.md`, Codex 앱 운용 방식은 `docs/CODEX_HARNESS_KO.md`, 현재 턴 목표와 최신 기준은 `BRIEF.md`를 본다.

## 1. 미션
항상 아래를 유지한다.
- raw bridge 철학 유지
- 20바이트 packet / CRC8 / DLC / `t_us` wrap 철학 유지
- live / replay 의미 분리
- 값 정합과 프로그램 완성도 분리
- 그래프는 truth-first
- Codex 앱에서는 **직접 수정 가능한 프로젝트 폴더 / Git 작업트리**를 기본 작업 단위로 본다

## 2. Source of Truth
판단 순서는 아래와 같다.
1. Codex 앱에서 현재 연 프로젝트 폴더와 현재 branch / worktree 상태
2. 사용자가 이번 턴 기준본이라고 지정한 최신 ZIP / 최신 소스 / 최신 handoff artifact
3. 현재 저장소의 `BRIEF.md`
4. 현재 턴 사용자 지시
5. 이 `AGENTS.md`
6. `docs/CODEX_HARNESS_KO.md`
7. 저장소 현재 파일
8. 과거 대화 / 과거 추론

항상 `BRIEF.md`를 먼저 읽고 시작한다.
기준본이 바뀌면 그 사실을 먼저 명시한다.

## 3. 저장소 맵
- `src/backend/`: session, replay, decode, alarm, graph, recorder 핵심 로직
- `qml/`: UI 및 페이지 구성
- `data/`: 모델팩 / 룰 / 신호 맵
- `scripts/`: Windows 빌드/배포 보조 배치
- `BRIEF.md`: 현재 턴 목표, 제약, acceptance, 기준본
- `docs/CODEX_HARNESS_KO.md`: Codex app / GitHub / MCP / harness 운용 지침
- `.agents/skills/main-code-progress/SKILL.md`: 메인 코드 진행용 정식 skill

## 4. Build / Run 기준
Windows + Qt 6.10.2 + MSVC2022 기준으로 본다.
- Release configure: `cmake --preset vs-release-qt6`
- Release build: `cmake --build --preset build-release`
- Debug configure: `cmake --preset vs-debug-qt6`
- Debug build: `cmake --build --preset build-debug`
- Release 배포 보조: `scripts\deploy_release.bat <build-folder-or-exe>`

빌드를 직접 검증하지 못했으면 성공을 단정하지 말고, 무엇을 확인하지 못했는지 적는다.

## 5. 항상 보존할 것
- 이미 잘 되는 기능을 깨뜨리며 새 정리를 넣지 말 것
- `BRIEF.md`의 `Working Features To Preserve`를 먼저 보호할 것
- live/replay recent-window graph와 full-range overview graph는 다른 도구로 유지할 것
- 전체 그래프 최적화 때문에 raw truth, peak, fixed-axis 의미가 바뀌면 안 됨
- timing 문제를 alarm 의미와 섞지 말 것

## 6. 작업 모드
- 복잡한 일은 먼저 계획부터 세운다
- 한 턴의 주목표는 1개, 부목표는 최대 2개로 제한한다
- unrelated 축 확장은 피한다
- 반복해서 틀린 규칙은 이 파일 또는 skill/docs에 반영해 다음 턴부터 재발을 줄인다
- 결과물 기본형은 **프로젝트 폴더 직접 수정 + 변경 요약 + 검증 상태**다

## 7. Build-risk Gate
아래를 건드리면 build-risk 턴이다.
- `AppController.h/.cpp`
- 신규 C++ 파일 추가
- `main.cpp`
- `CMakeLists.txt`
- QML 타입 등록 / import / module 구성
- signal / property / slot / `Q_PROPERTY`

build-risk 턴에서는:
- 목표를 더 좁힌다
- 연결된 파일을 같은 수정 세트에 넣는다
- unrelated UI/UX 변경을 같이 넣지 않는다
- 선언/정의/CMake/QML 등록 일치를 함께 점검한다
- MSVC 타입 혼합 위험(`int`, `qsizetype`, `size_t`)을 본다

## 8. 그래프는 특수 영역
그래프는 일반 UI가 아니라 truth / 성능 / 신뢰성이 동시에 걸린 핵심 병목이다.
- raw truth와 display path를 분리한다
- display 최적화 때문에 truth가 바뀌면 안 된다
- peak 보존
- 기본은 fixed axis
- micro zoom / nested zoom의 lock semantics 유지

그래프 턴에서 overview/settings/operator guidance 같은 unrelated 축까지 같이 넓히지 않는다.

## 8-1. Board / Typed Evidence 전환
`J_ArdP7_AM2_CSM (3).zip` 보드 handoff 이후 최종 방향은 `docs/BOARD_QT_FINAL_ARCHITECTURE.md`를 따른다.

항상 아래를 지킨다.
- legacy 20-byte packet은 호환 모드로 보존한다.
- production truth는 typed board stream 원본 byte다.
- CAN RX, CAN TX audit, voltage raw, board health/event, control ack는 서로 다른 evidence type이다.
- 전압 raw sample을 가짜 CAN frame으로 만들지 않는다.
- host가 요청한 CAN TX는 board가 `CAN_TX_RAW`를 낼 때까지 성공으로 보지 않는다.
- 제어 UI는 typed capture/replay parity, capability/health 표시, safety lease/heartbeat/arm/estop/audit chain 전까지 열지 않는다.

## 9. Codex 앱 기본 전달 방식
기본 결과물은 아래 조합이다.
- 현재 프로젝트 폴더의 직접 수정 결과
- 변경 파일 목록
- 핵심 변경점 요약
- build-risk / 미검증 리스크
- 사용자가 바로 확인할 테스트 포인트

다음은 **선택형 handoff**로 본다.
- patch ZIP
- handoff 문서
- export bundle

즉, patch ZIP은 Codex 앱의 기본 전제가 아니라 **필요할 때만 만드는 보조 전달 형식**이다.

## 10. Review guidelines
GitHub PR 리뷰 또는 `@codex review`에서도 아래를 우선 본다.
- replay / live 의미 혼선은 높은 심각도로 본다
- 그래프 truth drift, peak 손실, fixed-axis 붕괴는 높은 심각도로 본다
- `CMakeLists.txt` / QML 등록 누락은 높은 심각도로 본다
- 사용자 기준본을 무시한 변경은 설계 회귀로 본다
- Windows 실행/배포 경로 설명이 모호하면 setup risk로 본다

## 11. Done 정의
최소한 아래를 만족하는 방향으로 끝낸다.
- 이번 턴 주목표가 명확히 전진했음
- 회귀 가능성이 큰 축을 숨기지 않았음
- build / run / replay / graph 측면의 미검증 영역을 솔직히 남겼음
- `BRIEF.md`가 현재 상태를 반영함

## 12. 한 줄 요약
이 저장소에서 Codex는 **현재 프로젝트 폴더와 worktree를 기준으로, 기준본을 흔들리지 않게 잡고, 회귀 없이 완성도를 밀어 올리는 개발자**처럼 동작한다.
