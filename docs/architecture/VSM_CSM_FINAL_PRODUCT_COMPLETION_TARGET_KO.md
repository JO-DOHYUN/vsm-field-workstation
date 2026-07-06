# VSM/CSM Final Product Completion Target

## 1. Product Purpose

최종 제품은 실차 2-bus CAN을 관찰, 기록, 재생, 분석하는 Passive-Safe
evidence workstation이다. 제품의 첫 번째 성공 조건은 로그 저장량이 아니라
USB 연결/해제, VSM 실행/종료, Debug Tap on/off, MCU reset/boot 중 차량 CAN에
영향을 주지 않는 것이다.

현재 Portenta/MidCarrier/MCP2515/TJA1050 구성은 `Software Passive Prototype`
으로 표시한다. 외부 analyzer/scope/DTC artifact가 검증되기 전에는
`Verified Passive Product`로 표시하지 않는다.

## 2. Product Identity

- 제품은 무조건 2-bus ACK-capable observe-only monitor다.
- ACK-observe는 CAN bus의 정상 ACK 참여를 뜻한다. Host TX, control,
  downlink, test TX와 같은 의미가 아니다.
- Host-originated CAN TX/control/downlink/test TX는 Passive Product binary와
  VSM passive runtime에서 금지한다.
- Full/Instrumented 기능은 bench/lab 전용이며 실차 passive acceptance로 쓰지 않는다.
- 1-bus product/acceptance는 금지한다. 단, CSM capability가 one-bus/missing-bus일
  때는 제품 mismatch로 경고해야 한다.

## 3. User Scenarios

### Vehicle CAN connected, USB absent

CSM은 차량 CAN에 host-originated TX/control을 내지 않는다. 현재 prototype에서는
firmware가 실행되기 전 pin/transceiver 물리 상태는 코드만으로 증명할 수 없으므로
hardware evidence가 필요하다.

### USB plug while vehicle CAN is already connected

CSM은 boot/session bring-up 동안 `CAN_FRONTEND_PRESESSION_HOLD` 상태를 유지한다.
이 상태에서는 old payload replay와 host TX/control/downlink가 금지된다. 안정된
CDC/DTR session과 quiet window 후에만 `CAN_FRONTEND_SESSION_READY`가 올라오고
ACK-observe가 arm된다.

### VSM connect/log/debug

`vsm-capture-core.exe`만 COM/USB를 소유한다. UI는 Core view query consumer이고,
Debug Tap은 Core IPC sidecar다. Debug Tap은 COM을 열지 않고 Core state를 변경하지
않는다.

### USB unplug or core/app crash during capture

Active capture는 `.part`로 버려지면 안 된다. Core teardown은 capture writer queue를
drain하고, 가능한 경우 `capture.stream/index/session.meta.json/capture.diagnostics.json`
finalize를 먼저 시도한다. 실패 시 `.part`와 recovery report를 incomplete evidence로
분리한다.

## 4. Data Flow

```text
CSM 2-bus CAN front-end
  -> session hold / ACK-observe gate
  -> typed evidence stream
  -> vsm-capture-core.exe
       -> capture.stream/index authoritative truth
       -> bounded materialized views
       -> IPC ViewChanged + GetView
  -> vsm-ui.exe
       -> view snapshot apply only
  -> optional vsm-debug-tap.exe
       -> Core IPC subscription only
```

## 5. Ownership Rules

- `capture.stream/index`: Core capture writer owns authoritative truth.
- `live_latest`, decoded tail, graph, analysis rows: derived bounded views only.
- `AppController`: QML facade and view snapshot apply only.
- Debug Tap: non-owning observer. It may write its own artifacts but cannot mutate
  Core transport/capture/control state.
- Hardware passive fields in capability are claim/reference fields. VSM must not
  convert them to proof without external artifact validation.

## 6. CSM Completion Criteria

- Product default env: `portenta_h7_m7_mid_mcp2515_j4_dual_csm_passive`.
- Two-bus receive is required.
- Passive env compile-time guards reject host TX/control/downlink/test TX.
- USB reconnect reset is disabled.
- CAN front-end initialization is deferred until stable host session.
- Host absent/no-replay contract is enforced.
- Passive readback guard detects MCP mode/TXREQ violations.
- Fault/readback violation latches product-blocking diagnostics and returns to
  no-ACK hold where safe.
- Capability declares ACK capability separately from host TX/control capability.

## 7. VSM Completion Criteria

- UI launches/uses `vsm-capture-core.exe` for COM/USB ownership.
- Passive diagnostics launches `vsm-debug-tap.exe` as a third non-owning process.
- COM-owning gateway is blocked in passive profile.
- `capture.stream/index` remains the only truth.
- Active capture is finalized on stop, disconnect, and core teardown where possible.
- `.part` captures are incomplete evidence and never shown as normal finalized logs.
- Transport Detail separates configured passive, runtime passive, hardware unverified,
  and verified passive.
- Debug Tap trace is bounded/summary-first and cannot create hot-path pressure.

## 8. Acceptance Limits

Build success is not vehicle-impact-free proof. Final vehicle PASS requires:

- reference CAN analyzer error/passive/bus-off delta 0,
- scope evidence of no dominant pulse/glitch on USB plug/unplug/reset,
- DTC/ADCU latch delta 0,
- CSM CAN_TX_RAW/control/downlink count 0,
- passive violation/TXREQ violation 0,
- capture parity and recovery artifacts correct.

Until those are present, UI must report `Software Passive Prototype / Hardware Unverified`.
