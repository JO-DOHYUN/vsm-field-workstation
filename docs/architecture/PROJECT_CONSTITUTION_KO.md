# PROJECT_CONSTITUTION_KO

이 문서는 VSM Qt 애플리케이션과 CSM 펌웨어의 통합 제품 헌장이다. 세부 wire
format은 `docs/architecture/TYPED_STREAM_PROTOCOL_V1_KO.md`와
`shared/protocol/typed_stream_v1.md`를 따른다.

## Product Identity

VSM/CSM은 범용 USB-CAN 브리지나 단순 CAN 뷰어가 아니다. 실차 2-bus CAN을
차량에 영향을 주지 않고 관찰하며, 원본 typed evidence를 저장하고, replay,
decode, analysis, operator workflow를 evidence-first로 제공하는 field
workstation이다.

최종 제품의 기본 모드는 Passive Product다. Control, host TX, full
instrumentation, COM-owning gateway는 bench/lab profile이며 field passive
acceptance에 사용할 수 없다.

## Product Goals

1. Vehicle impact free: USB plug/unplug, VSM start/stop, Debug Tap on/off,
   capture start/stop, crash/reconnect가 차량 CAN에 영향을 주지 않아야 한다.
2. Evidence first: `capture.stream/index`가 유일한 authoritative truth다.
3. View separation: UI/live/raw tail/graph/analysis rows는 bounded materialized
   view이며 truth를 소유하지 않는다.
4. Passive proof separation: CSM capability의 hardware fields는 claim/reference다.
   `verified_passive`는 외부 analyzer/scope/DTC artifact 검증 전에는 금지한다.
5. Two-bus product: 제품은 2-bus RX-only passive monitor다. 1-bus product나
   acceptance는 없다. missing/one-bus mismatch는 blocking diagnostic이다.

## Non-Negotiable Invariants

- Production live path는 CSM typed evidence stream 전용이다.
- COM open은 board alive가 아니다. valid `CAPABILITY`와 fresh `BOARD_HEALTH`가
  필요하다.
- `CAN_RX_RAW`/`CAN_RX_SEGMENT`, `CAN_TX_RAW`, `ADC_SAMPLE`, `BOARD_EVENT`,
  `BOARD_HEALTH`, `CAPABILITY`, `CONTROL_ACK`는 서로 다른 evidence type이다.
- Host-requested TX, `CONTROL_ACK`, `CAN_TX_RAW`, feedback CAN RX는 같은 사건으로
  합치지 않는다.
- Passive Product profile에서 VSM은 serial read-only, RTS no-touch, host
  TX/control/gateway off다. DTR은 CSM capability가 session-only gate라고 선언할
  때만 허용한다.
- Debug Tap은 Core IPC sidecar다. COM/USB를 열지 않고 Core 상태를 변경하지
  않는다.
- `USB_ATTACH_QUARANTINE`은 CDC/uplink/session cleanup이다. CAN front-end
  passive drain을 멈추거나 MCP/transceiver를 reset/reconfigure하는 상태가 아니다.
- Kvaser/PCAN 단독 송신 테스트는 passive monitor가 ACK하지 않아 실패할 수 있다.
  Bench ACK/TX 검증과 vehicle passive monitor 검증은 분리한다.

## Architecture Contract

```text
CSM 2-bus passive CAN front-end
  -> typed evidence stream
  -> vsm-capture-core.exe
       - serial/USB sole owner
       - parser/storage/diagnostics/materialized view owner
  -> vsm-ui.exe
       - ViewChanged + GetView consumer
       - QML/model facade only
  -> optional vsm-debug-tap.exe
       - Core IPC read-only sidecar
       - default OFF
```

## Deferred Or Lab-Only Items

- Full closed-loop control
- Host CAN TX/control cycle in field passive mode
- COM-owning debug gateway in field passive mode
- CAN FD/DLC > 8
- Removing legacy `.bin` replay/import compatibility
- Claiming vehicle-impact-free PASS without analyzer/scope/DTC evidence
