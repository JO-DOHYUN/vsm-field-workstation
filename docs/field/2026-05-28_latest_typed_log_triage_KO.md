# 2026-05-28 Latest Typed Log Triage

기준 경로: `C:\WORKS\VS\turn81_full_buildfix2\replay_data\logs`

## 대상

- 최신 typed 실차 후보: `typed_capture_20260528_131402.typed`
- 상태: `capture.stream.part`, `capture.index.part`, `session.meta.json.part`, `events.jsonl.part`
- 의미: 앱/노트북 종료 또는 session finalize 전 중단으로 보인다. 다만 `capture.stream.part` 자체는 CRC/length/seq/trailing fault 없이 파싱됐다.

## 파싱 요약

`typed_capture_20260528_131402.typed/capture.stream.part`

- stream bytes: 36,864,308
- records: 898,200
- parser faults: crc 0, length 0, seq gap 0, trailing 0
- record counts: `CAN_RX_RAW 887,238`, `ENC_DERIVED 9,574`, `BOARD_EVENT 669`, `BOARD_HEALTH 479`, `CAPABILITY 240`
- `CONTROL_ACK`, `CAN_TX_RAW`: 없음. 이 capture만으로 host control TX 성공/실패는 판정할 수 없다.

CAN RX:

- bus0: 331,781 frames
- bus1: 555,457 frames
- bus0 DLC: `1=4,543`, `3=23,132`, `4=18,345`, `5=101,721`, `8=184,040`
- bus1 DLC: `1=36,931`, `5=91,778`, `8=426,748`

Board health:

- first `canRxTotal=1,576,829`, `canDroppedTotal=0`, `canFifoOverflowTotal=7,587`
- last `canRxTotal=2,463,794`, `canDroppedTotal=0`, `canFifoOverflowTotal=12,113`
- session delta: board RX +886,965, dropped +0, FIFO overflow +4,526
- last safety state 1, fault flags 0, queue depth 0

Capability:

- bus0: backend `MCP2515`, rx/tx/control enabled, max live DLC 8, 500 kbit/s, termination policy 3, isolation 1
- bus1: backend `ArduinoCAN`, rx/tx/control enabled, max live DLC 8, 500 kbit/s, termination policy 0, isolation 0

Board events:

- repeated `MCP2515_ERROR` events were present.
- This aligns more with bus0/MCP2515-side error/overflow pressure than with Qt replay/parser corruption.

## 판단

- "보드가 전부 DLC 8로 저장한다"는 의심은 이 로그 기준으로는 맞지 않다. typed payload의 DLC는 bus0/bus1 모두 다양하게 남아 있다.
- 최신 capture에서 bus0도 RX는 들어왔다. 즉 "bus0 완전 무수신"보다는 field wiring/termination/hot-plug/error 상태에서 MCP2515 쪽이 불안정해지는 쪽이 더 강한 후보다.
- `canDroppedTotal=0`인데 `canFifoOverflowTotal`이 증가했다. 현재 펌웨어 health naming상 FIFO overflow가 별도 누적되고 있으므로 UI에서 이 값을 크게 보여야 한다.
- `CONTROL_ACK`/`CAN_TX_RAW`가 없어 control 송신 성공/실패 증거는 없다. control 판단은 별도 제어 시도 capture가 필요하다.
- capture가 `.part`라서 현장 종료 상태는 비정상/부분 기록으로 표시해야 한다. 단, stream 자체는 파싱 무결성이 좋다.

## 이번 코드 반영

- Replay typed diagnostics에 `can_bus`, `can_dlc`, `board_health`, `board_events`, `capability_bus` row를 추가했다.
- `BOARD_EVENT` code 이름을 CSM firmware enum 기준으로 확장했다.
- session log에서 반복된 `ReplayPage.qml:80: ReferenceError: replaySlider is not defined`를 막기 위해 ReplayPage의 full replay slider를 복구했다.

## 다음 현장 확인 포인트

- hot-plug 직후 `MCP2515_ERROR` detail/counter가 급증하는지 본다.
- bus0 MCP2515 쪽 `canFifoOverflowTotal` 증가율과 실제 차량 CAN error frame 발생 시점을 맞춘다.
- 제어 시도 시에는 `CONTROL_ACK`와 matching `CAN_TX_RAW`가 남는 capture를 따로 확보한다.
