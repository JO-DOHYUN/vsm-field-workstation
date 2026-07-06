# START_HERE_KO

이 저장소는 VSM field workstation 본체다. 현재 제품 기준은 "USB-CAN
개발 도구"가 아니라, 차량 CAN에 영향을 주지 않는 2-bus Passive-Safe
evidence workstation이다.

## 먼저 읽을 순서

1. `AGENTS.md`
2. `BRIEF.md`
3. `INDEX.md`
4. `docs/architecture/VSM_CSM_PRODUCT_IDENTITY_KO.md`
5. `docs/architecture/VSM_CSM_FINAL_PRODUCT_COMPLETION_TARGET_KO.md`
6. 이번 턴과 직접 관련된 skill 또는 설계 문서

## 제품 아이덴티티

VSM/CSM은 실차 2-bus CAN을 관찰, 기록, 재생, 분석하는 evidence-first
제품이다. 첫 번째 정체성은 "차량에 영향을 주지 않는 수동 증거 수집기"다.
제어, 송신, 테스트 기능은 제품 기본 경로가 아니며 bench/lab 프로파일로만
격리한다.

기본 실행 구조는 Passive-Safe 2+1이다.

- `vsm-ui.exe`: operator workflow와 view 표시. COM/USB를 소유하지 않는다.
- `vsm-capture-core.exe`: COM/USB 단독 소유, typed evidence capture,
  bounded materialized view 생성.
- `vsm-debug-tap.exe`: 기본 OFF. 필요할 때만 Core IPC view를 구독하는
  non-owning sidecar. COM/USB를 열지 않고 Core 상태를 변경하지 않는다.

## 절대 보존할 원칙

- `capture.stream/index`만 authoritative capture truth다.
- UI, graph, decoded tail, analysis row는 bounded materialized view다.
- 제품은 2-bus ACK-capable observe-only monitor다.
- 1-bus 제품 또는 1-bus acceptance는 금지한다. 단, missing/one-bus
  capability mismatch 경고는 반드시 유지한다.
- ACK-observe는 host TX/control이 아니다. Host-originated CAN TX,
  downlink, control cycle은 Passive Product에서 금지한다.
- COM open만으로 board alive로 보지 않는다. valid `CAPABILITY`와 fresh
  `BOARD_HEALTH`가 필요하다.
- `USB_ATTACH_QUARANTINE`은 CDC/uplink/session payload quarantine이다.
  CAN front-end drain 정지로 해석하지 않는다.
- Hardware passive evidence는 capability claim/reference일 뿐이다.
  verified passive는 external analyzer/scope/DTC artifact 검증 전에는
  표시하지 않는다.

## 작업 원칙

데이터 흐름 변경은 항상 아래 순서로 한다.

1. data flow diagram
2. owner / consumer / drop policy
3. 기존 owner 위반 검색
4. boundary DTO/interface 추가
5. 기능 이동
6. 구 경로 삭제
7. static/test guard 추가

파일 분리는 아키텍처 분리가 아니다. `TypedRecordList`, `FrameRecordList`,
diagnostics payload, `AppController` 임시 상태가 여러 경계를 먹는 구조는
제품 완성으로 보지 않는다.
