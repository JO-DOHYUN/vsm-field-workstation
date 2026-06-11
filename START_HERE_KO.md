# START_HERE_KO

VSM/CSM evidence-first CAN monitor/logger/replay/decode/control workstation 프로젝트의 새 계정 온보딩 진입점이다.

이 문서는 채팅 히스토리가 없는 Codex 계정이 현재 폴더만 보고도 작업 목적, 기준 폴더, 검증 상태, 다음 작업 축을 바로 파악하도록 둔다.

## 읽기 순서

1. `AGENTS.md`: 상위 운영 규칙과 불변조건
2. `START_HERE_KO.md`: 새 계정용 현재 프로젝트 지도
3. `BRIEF.md`: 현재 기준본, 즉시 다음 작업
4. `INDEX.md`: 문서 허브
5. `docs/runbooks/STANDALONE_GIT_WORKFLOW_KO.md`: VSM 단독 git repo 운영 기준
6. 이번 턴과 직접 관련된 `.agents/skills/` 또는 `docs/` 문서만 추가

## 현재 기준 폴더

- Active VSM repository root: 이 폴더 자체.
- Local working path in this machine: `C:\WORKS\VS\turn81_full_buildfix2`
- GitHub repository: `JO-DOHYUN/vsm-field-workstation`
- CSM firmware는 별도 workspace/repository에서 관리한다. VSM 작업 중에는 사용자가 펌웨어 build나 CSM 수정을 명시할 때만 CSM 폴더를 직접 기준으로 삼는다.

## 제품 정체성

이 프로젝트는 단순 CAN 뷰어가 아니다.
CSM 보드의 typed evidence stream을 production truth로 삼아 CAN 수신, 제어 요청, 보드 수락, 실제 CAN 송신 audit, replay, decode, graph, control operator workflow를 분리 증거로 남기는 현장용 workstation이다.

항상 지킬 기준:

- live production path는 CSM typed evidence stream 전용이다.
- COM open만으로 board alive로 보지 않는다. valid `CAPABILITY`와 fresh `BOARD_HEALTH`를 기준으로 판단한다.
- host 요청 TX, `CONTROL_ACK`, `CAN_TX_RAW`, feedback `CAN_RX_RAW`는 서로 다른 evidence다.
- 실제 CAN 송신 성공은 matching `CAN_TX_RAW` audit만 기준으로 한다.
- legacy 20-byte packet, CRC8, DLC, `t_us` wrap은 replay/import compatibility로 보존한다.
- live와 replay 의미는 분리한다.
- graph는 truth-first, fixed-axis, peak 보존 기준을 유지한다.

## 현재 운영 데이터 위치

- Runtime log/capture root: `replay_data/`
- Session logs and typed captures: `replay_data/logs/`
- Snapshot/export staging: `replay_data/snapshots/`

`replay_data`는 운영 데이터 위치이며 git source가 아니다. README 파일만 추적하고 실제 capture/log/binary 산출물은 ignore한다.

## 현재 검증 상태

마지막으로 기록된 local gate 기준:

- Current workspace Release configure/build passed.
- Current workspace full `ctest` passed: 22/22.
- Release exe startup smoke passed.
- Latest portable Field RC generated and smoke passed: `out/build/x64-Release/portable_field_rc_20260604`.
- CSM `portenta_h7_m7_mid_mcp2515_j4_dual_csm` PlatformIO build passed.
- GitHub Actions `platform-ci` passed on commit `b17e1c4`.

최신 프로그램 마감 기준은 Release build, full `ctest`, exe startup smoke, portable deploy smoke, CSM PlatformIO build를 모두 확인한 RC 산출물이다.

현재 미검증으로 남길 영역:

- 새 HIL/실차 CAN 연결 안정성
- 새 보드 펌웨어 플래시 후 typed stream parity
- 실제 차량 기준 control feedback/tx audit 일치성

## 현재 코드/기능 상태

- Storage/Replay path ownership, TransportRuntime, Typed/Legacy ingress, EvidenceRuntime, ControlRuntime, ControlAuditModel, HostTxRuntime, ControlCycleRuntime 분리는 완료된 기준으로 본다.
- ControlPage는 요청/쓰기/ACK/`CAN_TX_RAW`/feedback/fault를 분리하고, 실제 TX 성공은 matching `CAN_TX_RAW`만 기준으로 한다.
- Replay는 legacy `.bin`과 typed capture session을 모두 열며 typed diagnostics, DLC verdict, bus/capability/health/event rows를 제공한다.
- Live/Replay/Control/Graph/주요 UI 상태는 QML smoke/state probe로 회귀 방지선을 가진다.
- 실차를 제외한 남은 프로그램 마감 작업은 최신 portable RC 생성, 문서 최신화, synthetic load/replay/UI smoke, release checklist closure다.

## 금지할 임시 구현 패턴

- 나중에 Runtime으로 뺄 책임을 `AppController`에 임시로 붙여서 끝내지 않는다.
- 경로, replay, storage, transport, control audit처럼 소유자가 명확한 책임은 처음부터 runtime boundary와 테스트 계획을 같이 잡는다.
- 기능을 없애서 UI/성능/빌드 문제를 숨기지 않는다.
- HIL/실차를 하지 않았으면 성공으로 단정하지 않는다.

## 검증 기준

검증은 변경면 기반 ladder를 따른다.

- Level 0: 문서/상수/좁은 QML 문구. static search와 conflict marker/link 확인.
- Level 1: 단일 subsystem. 관련 target build와 subset test.
- Level 2: `AppController`, `SerialWorker`, `TypedRecords`, CMake, QML registration, runtime boundary. Release build와 관련 `ctest -R`.
- Level 3: PR-ready 또는 runtime boundary 완료. Release build, full `ctest`, exe startup smoke, CSM PlatformIO build.

자세한 기준은 `docs/ai_harness/BUILD_VERIFY_POLICY_KO.md`와 `.agents/skills/qt-build-verify/SKILL.md`를 따른다.
