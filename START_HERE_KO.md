# START_HERE_KO

이 저장소는 VSM 제품 본체이다. 현재 제품 기준은 "USB-CAN 도구"가 아니라
실차 CAN에 영향을 주지 않는 Passive-Safe evidence workstation이다.

## 먼저 읽을 순서

1. `AGENTS.md`
2. `BRIEF.md`
3. `INDEX.md`
4. `docs/architecture/VSM_CSM_PRODUCT_IDENTITY_KO.md`
5. 이번 작업에 직접 필요한 skill 또는 설계 문서

문서와 코드를 수정할 때는 이 순서를 벗어나 과거 히스토리 문서를 기준으로
삼지 않는다. 과거 실험/게이트웨이/제어 bring-up 문서는 참고 자료일 뿐 제품
기준이 아니다.

## 제품 아이덴티티

VSM/CSM은 실차 2-bus CAN을 관찰하고, 기록하고, 재생하고, 분석하는
evidence-first 제품이다. 최종 제품의 첫 번째 정체성은 "차량에 영향을 주지
않는 수동 증거 수집기"이며, 제어/송신/실험 기능은 제품 기본 경로가 아니다.

기본 실행 구조는 Passive-Safe 2+1이다.

- `vsm-ui.exe`: UI와 operator workflow. COM/USB를 소유하지 않는다.
- `vsm-capture-core.exe`: COM/USB 단독 소유, typed evidence capture, bounded
  materialized view 생성.
- `vsm-debug-tap.exe`: 기본 OFF. 필요할 때만 Core IPC view를 구독하는
  non-owning sidecar. COM/USB를 열지 않고 Core 상태를 바꾸지 않는다.

## 최종 목적

- 실차 CAN에 영향 0인 2-bus passive monitor/logger/replay/decode workstation.
- `capture.stream/index`를 유일한 authoritative evidence truth로 보존.
- UI/live/raw tail/graph/analysis는 모두 bounded materialized view로만 표시.
- CSM capability/health/event는 claim/evidence로 분리하고, hardware passive
  proof는 외부 analyzer/scope/DTC artifact 없이는 PASS로 주장하지 않는다.
- Host TX/control/full instrumentation은 bench/lab profile 전용으로 격리한다.

## 절대 보존할 계약

- VSM production live path는 CSM typed evidence stream 전용이다.
- COM open만으로 board alive로 보지 않는다. valid `CAPABILITY`와 fresh
  `BOARD_HEALTH`가 필요하다.
- Passive Product profile에서 VSM은 serial read-only로 열고, CSM이
  `usb_cdc_dtr_session_only=1`을 선언한 경우에만 session gate 목적의 DTR을
  허용한다. RTS, host TX, control cycle, COM-owning gateway는 금지한다.
- 제품은 2-bus passive monitor다. 1-bus product/acceptance는 금지한다.
  단, missing/one-bus capability mismatch 경고는 반드시 유지한다.
- `USB_ATTACH_QUARANTINE`은 CDC/uplink/session payload quarantine이지 CAN
  front-end 정지가 아니다.
- Kvaser/PCAN 단독 송신 테스트는 passive monitor가 ACK하지 않으므로 실패할 수
  있다. 이는 vehicle passive monitor 실패 판정과 분리한다.

## 작업 원칙

데이터 흐름 변경은 항상 다음 순서로 한다.

1. data flow diagram
2. owner / consumer / drop policy
3. 기존 owner 위반 검색
4. boundary DTO/interface 추가
5. 기능 이동
6. 구 경로 삭제
7. static/test guard 추가

파일 분리는 아키텍처 분리가 아니다. `TypedRecordList`, `FrameRecordList`,
diagnostics payload, AppController 임시 상태가 여러 경계를 먹는 구조는 제품
완성으로 보지 않는다.
