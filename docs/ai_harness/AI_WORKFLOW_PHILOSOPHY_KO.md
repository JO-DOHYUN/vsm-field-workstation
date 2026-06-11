# AI_WORKFLOW_PHILOSOPHY_KO

이 문서는 VSM/CSM 프로젝트에서 Codex가 토큰 대비 결과물을 높이기 위해 지켜야 할 작업 철학이다.

## 한 턴의 단위

한 턴은 파일 N개 수정이 아니라 사용자 기능 한 축의 vertical slice다.

완료된 slice는 가능하면 아래 중 최소 3개를 포함한다.

- 사용자 화면 또는 작업 흐름 변화
- evidence/model/runtime 계약 변화
- 회귀 테스트 또는 QML state probe
- 실행 프로그램 기준 smoke

작은 버그를 하나씩 흩어서 고치지 말고, 사용자가 실제 현장에서 겪는 하나의 축을 끝내는 단위로 묶는다.

## Evidence-First 기준

현장 판단은 화면 텍스트가 아니라 evidence contract로 고정한다.

- CAN RX, host TX request, `CONTROL_ACK`, `CAN_TX_RAW`, feedback은 섞지 않는다.
- board alive는 COM open이 아니라 valid `CAPABILITY`와 fresh health evidence다.
- replay와 live는 같은 UI에 보이더라도 의미와 source state를 분리한다.
- 실차/HIL을 하지 않았으면 자동 테스트 결과로 대체 단정하지 않는다.

## Runtime Boundary First

나중에 Runtime으로 뺄 책임이면 처음부터 boundary를 같이 설계한다.

- `AppController`는 QML facade와 orchestration으로 축소한다.
- storage path, replay load/cache, transport ownership, board health, control audit은 `AppController`에 임시 누적하지 않는다.
- 구현 난이도 때문에 임시로 넣어야 한다면 같은 slice 안에 owner, extraction target, test/rollback 조건을 문서화한다.
- 다음 코드 축의 1순위는 `RuntimePaths`와 저장/replay 경로 책임을 `StorageRuntime`/`ReplayRuntime` 쪽으로 분리하는 것이다.

## 검증 토큰 절약

검증은 무조건 full build가 아니라 변경면 기반 ladder로 선택한다.

- iteration 중에는 subset-first로 증명한다.
- runtime boundary, PR-ready, release/deploy gate에서만 Level 3 full 검증으로 올린다.
- 성공 로그는 요약하고, 실패 로그는 원인이 되는 첫 에러와 다음 복구 단계만 보고한다.
- 반복적으로 필요한 smoke는 QML state probe나 작은 테스트로 못 박는다.

## 문서 운용

- `BRIEF.md`는 현재 기준만 둔다.
- 설계 근거는 `docs/`, 오래된 기록은 `history/`로 보낸다.
- root에는 entry 문서와 build/config만 둔다.
- 새 계정은 `AGENTS.md -> START_HERE_KO.md -> BRIEF.md -> INDEX.md` 순서로 시작한다.

## 금지 패턴

- 기능을 제거해 build/UI/performance 문제를 숨기기
- successful run을 직접 실행하지 않고 이번 턴 성공처럼 보고하기
- 실차 문제를 자동 테스트만으로 해결됐다고 단정하기
- 새 runtime owner가 명확한 책임을 `AppController`에 계속 쌓기
- root에 긴 handoff나 임시 prompt를 계속 추가하기
