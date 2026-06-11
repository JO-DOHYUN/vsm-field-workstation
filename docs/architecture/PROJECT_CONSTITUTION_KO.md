# PROJECT_CONSTITUTION_KO

이 문서는 VMS Qt 프로그램과 CSM 보드의 통합 제품 원칙이다.
세부 binary 계약은 [[TYPED_STREAM_PROTOCOL_V1_KO]], 제어 증거 의미는 [[CONTROL_EVIDENCE_CONTRACT_KO]], VMS 런타임 구조는 [[VMS_ARCHITECTURE_KO]]를 따른다.

## Product Identity

VMS-CSM은 단순 USB-CAN 브릿지나 화면형 CAN 뷰어가 아니다.
Portenta H7 기반 CSM 보드와 Qt/C++ 기반 VMS 앱을 묶어, 명령 의도, 보드 수락, 실제 CAN 송신, 외부 CAN 수신, 전압/엔코더 원신호, 보드 건강상태를 분리 증거로 남기는 산업형 CAN monitoring/control workstation으로 본다.

## Non-Negotiable Invariants

- VMS live production path는 CSM typed evidence stream 전용이다.
- COM open은 physical serial open일 뿐이며, board alive는 valid `CAPABILITY` 수신과 protocol/profile match를 기준으로 판단한다.
- legacy 20-byte packet은 삭제하지 않고 기존 BIN replay/import compatibility로 보존한다.
- typed stream 원본 byte가 production truth다.
- `CAN_RX_RAW`, `CAN_TX_RAW`, `ADC_SAMPLE`, `BOARD_EVENT`, `BOARD_HEALTH`, `CAPABILITY`, `CONTROL_ACK`는 서로 다른 evidence type이다.
- ADC/전압/health/event/encoder evidence를 fake CAN frame으로 만들지 않는다.
- Qt command write, `CONTROL_ACK`, `CAN_TX_RAW`, feedback `CAN_RX_RAW`는 각각 다른 사건이다.
- 실제 CAN 송신 성공 기준은 board가 발행한 matching `CAN_TX_RAW` audit이다.
- graph truth, fixed-axis, peak preservation, live/replay source separation은 계속 보존한다.
- VMS serial parsing과 blocking transport work는 UI thread에서 수행하지 않는다.

## Current Adoption Decision

최신 통합 아키텍처 결정서는 [[VMS_CSM_03_ARCHITECT_SYNTHESIS_FINAL]]이다.
단, 이 문서는 기존 `AGENTS.md`와 `BRIEF.md`를 덮어쓰는 지시문이 아니라, 기존 하네스 운용 기준 안으로 병합할 설계 목표다.

## Deferred Items

- Qt 6.11/6.10.3 upgrade
- Portenta M4 split
- CAN FD or DLC > 8 support
- full closed-loop control
- feedback ID/scale invention without CAN/model evidence
- large UI redesign before evidence models exist
- removing legacy `.bin` replay/import
- direct 24V encoder/voltage input assumptions without hardware protection contract
