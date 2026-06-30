# Current Correction - 2026-06-30

- Product passive CSM is two-bus RX only.
- portenta_h7_m7_mid_mcp2515_j4_dual_csm_passive must expose bus0 MCP2515 listen-only and bus1 Mid Carrier J4/U2 silent-monitor RX.
- portenta_h7_m7_mid_mcp2515_j4_dual_csm_passive_2bus_rx is only a compatibility alias, not a separate product direction.
- Any passive upload that exposes fewer than two RX buses is an operator/build-profile error and must not be used as field product evidence.
- If any passive product bus advertises normal mode, ACK capability, or error-frame capability, VSM must classify the CSM as locked_vehicle_impact_possible.
# VSM/CSM CDC 유지형 Passive Product 최종 플랜

작성 기준: 2026-06-30  
대상 기준:

- VSM: `11e9470 Productize passive debug tap plane`
- CSM: `a65b582 Report USB CDC session close events`
- 전제: **USB CDC는 당장 유지한다.** 단, CDC는 제품의 핵심 안전 경계가 아니라 **evidence uplink**로만 취급한다.
- 목표: PCAN/Kvaser급 CAN analyzer처럼 **USB 연결/제거/앱 종료/디버그 탭 동작이 차량 CAN bus를 흔들지 않는 제품형 passive monitor**로 올린다.

---

## 0. 최종 단일 결론

현재 프로젝트의 최종 제품 방향은 다음 하나로 고정한다.

```text
CDC-maintained Vehicle-Bus-Non-Interfering Passive CAN Probe

= CDC는 유지하지만, CDC/USB/VSM lifecycle이 CAN front-end에 절대 영향을 주지 않는
  차량 CAN 비간섭형 passive probe 구조
```

따라서 결론은 두 갈래가 아니다.

- **CSM은 “USB CDC logger”가 아니라 “차량 CAN 비간섭형 passive probe”로 재정의한다.**
- **VSM은 “CAN에 영향을 주는 프로그램”이 아니라 “CSM passive evidence를 검증·표시·기록하는 product workstation”으로 재정의한다.**
- **CDC는 유지하지만, CDC session open/close/DTR/re-enumeration이 MCP2515/CAN transceiver/reset/TX gate에 영향을 주면 제품 실패다.**
- **정정: bus1 미표시는 VSM 버그가 아니라 잘못된 CSM build/upload 선택으로 발생한 운용 오류였다. 제품 passive profile은 항상 2-bus RX이며, 2개 미만 RX bus를 광고하는 build/upload는 실차 제품 근거로 금지한다.**

---

## 1. 객관 검증 근거 요약

### 1.1 산업 장비 기준

PCAN/Kvaser급 CAN analyzer의 핵심은 단순히 “수신 가능”이 아니다. 제품 기준은 다음이다.

```text
앱이 죽어도,
USB가 빠져도,
드라이버가 재연결돼도,
MCU가 재부팅돼도,
CAN bus를 dominant로 밀 수 없어야 한다.
```

공개 자료 기준:

- Kvaser silent mode는 CAN data frame, error frame, ACK slot까지 송신하지 않고 CAN bus를 dominant로 구동하지 않는 상태로 설명된다.
- PEAK PCAN listen-only는 CAN controller가 active event에 참여하지 않고 passive monitor로 traffic을 분석하는 모드로 설명된다.
- TJA1051 계열 transceiver의 Silent mode는 transmitter를 disable하고 bus를 recessive로 둔 채 receiver는 유지한다. Off mode는 bus pin floating으로 network에서 invisible 상태가 될 수 있다.
- TCAN1044A-Q1 같은 automotive CAN transceiver는 TXD dominant timeout으로 TXD가 low에 고착돼도 bus를 계속 dominant로 잡지 못하게 한다.

이 기준으로 보면, 우리 CSM의 상위 목표는 **MCP2515 listen-only 설정 한 줄이 아니라 CAN front-end 전체의 fault containment**다.

### 1.2 현재 CSM 코드 기준 확인

현재 `a65b582` CSM에서 확인되는 사실:

```text
passive env:
- BOARD_CSM_PROFILE_PASSIVE_PRODUCT=1
- BOARD_ENABLE_BUILTIN_CAN_RX=0
- BOARD_ENABLE_HOST_CAN_TX=0
- BOARD_ENABLE_HOST_DOWNLINK=0
- BOARD_MCP2515_LISTEN_ONLY_BY_DEFAULT=1
- BOARD_USB_CDC_RECONNECT_RESET_MS=0
- BOARD_PASSIVE_TRANSCEIVER_RESET_SAFE=0
- BOARD_PASSIVE_HARDWARE_SAFETY_CASE_ID=0
- BOARD_PASSIVE_BENCH_VERIFICATION_ID=0
```

의미:

- Host TX/control/downlink는 잘 꺼져 있다.
- USB disconnect watchdog reset은 꺼져 있다.
- MCP2515는 listen-only로 초기화된다.
- 하지만 transceiver reset-safe, hardware safety case, bench verification은 아직 0이다.
- 즉 현재 CSM은 **passive candidate**이지 **verified passive product**가 아니다.

추가 확인:

- `uplink_host_session_open()`은 CDC에서 `_SerialUSB.connected()`를 본다.
- `enqueue_typed_record()`는 CDC session이 닫히면 false를 반환한다.
- `a65b582`는 USB CDC session close event 28을 추가했고, close event는 다음 session open 때 보고된다.

이 구조는 관측성은 좋아졌지만, USB 탈착이 차량 CAN에 영향을 안 준다는 것을 증명하지는 않는다.

### 1.3 현재 VSM 코드 기준 확인

현재 `11e9470` VSM에서 확인되는 사실:

- `vsm-debug-tap.exe`가 추가됐다.
- debug tap은 COM/USB를 열지 않고 `CoreIpcClientRuntime`으로 Core IPC view만 구독한다.
- 기존 COM-owning gateway는 Full/Instrumented lab-only로 밀렸다.
- VSM은 Capability V5의 passive policy, DTR session required/only, vehicle impact state 등을 decode하는 방향으로 진전했다.

의미:

- 최신 VSM에서 “GW켜기”는 COM을 하나 더 잡는 구조로 보면 안 된다.
- 현재 debug tap 문제는 COM 경합이 아니라 Core IPC lifecycle 문제다.
- VSM의 남은 역할은 차량에 영향 안 주는 것보다 **CSM 상태를 오해 없이 보여주는 것**이다.

---

## 2. 사용자 실제 사용 시나리오 검증

아래는 사용자가 실제 현장에서 겪을 실행 경로를 기준으로 한 제품 검증이다.

---

### 시나리오 A — 차량 CAN에 CSM 먼저 연결, USB는 아직 미연결

#### 사용자의 실제 행동

```text
1. 차량 전원 ON 또는 CAN bus live 상태
2. CSM CANH/CANL/GND를 차량 harness에 연결
3. 아직 노트북/VSM USB는 연결하지 않음
```

#### 제품 기대 동작

```text
- CSM이 CAN bus에 ACK를 보내지 않음
- error frame을 보내지 않음
- TXD 또는 transceiver가 dominant를 만들지 않음
- MCP2515는 listen-only
- transceiver는 silent/off/RX-only safe 상태
- USB 미연결이어도 CAN front-end는 안정
```

#### 현재 구조의 객관 판정

현재 코드는 MCP2515 listen-only 초기화까지는 맞다. 그러나 transceiver-level silent/off 검증이 없다. 특히 passive env의 `BOARD_PASSIVE_TRANSCEIVER_RESET_SAFE=0` 때문에 제품 PASS로 볼 수 없다.

#### 요구 개선

```text
CSM:
- MCP2515 CANCTRL mode readback guard 추가
- TXB0/TXB1/TXB2 TXREQ readback guard 추가
- transceiver silent/off/TXD inhibit 회로 또는 제어 정책 추가
- USB 미연결 상태에서도 CANH/CANL dominant glitch 0 검증

VSM:
- 이 시나리오에서는 아직 VSM이 없으므로 관여하지 않음
- 단, 나중에 연결됐을 때 이 pre-USB 상태의 boot/passive evidence를 capability/health로 확인해야 함
```

---

### 시나리오 B — 차량 CAN 연결 상태에서 USB를 노트북에 꽂음

#### 사용자의 실제 행동

```text
1. CSM은 차량 CAN에 물려 있음
2. 사용자가 USB-C/USB-A 케이블을 노트북에 꽂음
3. Windows가 CDC 장치를 인식함
4. VSM 실행 전 또는 실행 중일 수 있음
```

#### 제품 기대 동작

```text
- USB VBUS/GND 변화가 차량 CAN에 전기적 영향을 주지 않음
- MCU reset 없음
- MCP2515 reset 없음
- MCP2515 normal mode 진입 없음
- transceiver mode 변화 없음
- CANH/CANL dominant glitch 없음
- 차량 DTC/CAN error 증가 없음
```

#### 현재 구조의 객관 판정

코드상 `BOARD_USB_CDC_RECONNECT_RESET_MS=0`이므로 CDC disconnect로 인한 software reset은 막힌다. 하지만 USB plug-in 때 생길 수 있는 VBUS/GND bounce, bootloader/DTR side effect, MCP 전원 흔들림, SPI glitch, transceiver pin glitch는 코드만으로 닫히지 않는다.

#### 요구 개선

```text
CSM:
- USB plug-in event 전후로 MCP CANCTRL/CANINTF/EFLG snapshot 기록
- USB plug-in event 전후로 passive_violation_latch 유지 확인
- USB VBUS/GND 영향 측정 포인트 정의
- 가능하면 USB isolator 또는 CAN isolated transceiver 검토
- PC USB VBUS가 CSM/CAN side를 back-power하지 않도록 ideal diode/load switch 검토

VSM:
- USB_CDC_SESSION_OPEN event 27 표시
- DTR required/session-only bit 표시
- CAPABILITY 수신 시 dtr_reset_sensitive, passiveAcceptanceAllowed, vehicleImpactState 표시
- USB 연결 직후 MCP error/FIFO overflow가 증가하면 “VSM 문제”가 아니라 “CSM front-end instability”로 분류
```

---

### 시나리오 C — VSM passive product 실행

#### 사용자의 실제 행동

```text
1. VSM 실행
2. COM 포트 선택 또는 자동 연결
3. passive product profile로 Core가 serial을 엶
4. CAPABILITY/BOARD_HEALTH 수신 대기
```

#### 제품 기대 동작

```text
- VSM은 COM을 read-only/passive policy로 열어야 함
- DTR이 필요한 경우 CSM capability가 dtr_session_required=1, dtr_session_only=1을 광고해야 함
- VSM은 DTR session-only를 차량 영향 없음으로 검증할 evidence가 없으면 verified passive로 표시하면 안 됨
- CAPABILITY/BOARD_HEALTH가 없으면 connected가 아니라 진단 상태로 남아야 함
```

#### 현재 구조의 객관 판정

VSM은 passive debug tap을 COM-owning gateway에서 분리했다. 이는 맞는 방향이다. 그러나 UI가 사용자의 오해를 막으려면 연결 상태를 더 세밀하게 나눠야 한다.

#### 요구 개선

VSM connection state를 다음처럼 분리한다.

```text
Disconnected
PortOpenNoBytes
TypedSyncSearching
WaitingCapability
CapabilityRejected
WaitingBoardHealth
BoardHealthStale
PassiveCandidateHardwareUnverified
VerifiedPassive
IncompleteVehicleProfile
```

문구 예시:

```text
Serial open: yes
Bytes received: 0
Runtime profile: passive_product
DTR policy: session-only allowed by CSM capability: unknown
Diagnosis: CDC session not established or board not emitting CAPABILITY
```

---

### 시나리오 D — VSM capture 중 USB를 뽑음

#### 사용자의 실제 행동

```text
1. VSM capture 시작
2. CAN frame 수신 중
3. 사용자가 실수 또는 현장 상황으로 USB를 뽑음
```

#### 제품 기대 동작

```text
CSM:
- 차량 CAN에는 영향 없음
- MCP2515는 계속 listen-only
- transceiver는 계속 silent/RX-safe
- host 미연결 중에도 MCP RX FIFO를 drain하거나 discard-safe 처리
- 다음 USB open 때 close duration, missed frame summary, MCP/FIFO 상태를 보고

VSM:
- capture는 incomplete로 표시
- .part를 정상 finalized log처럼 보여주지 않음
- capture invalid/incomplete reason을 남김
```

#### 현재 구조의 객관 판정

`a65b582`는 session close를 다음 open 때 event 28로 보고한다. 그러나 CDC closed 중 typed enqueue는 false이므로 raw evidence delivery는 멈춘다. 이때 MCP FIFO drain이 계속 안정적으로 되는지는 별도 검증이 필요하다.

#### 요구 개선

CSM에 **Host Absent Drain Mode**를 추가한다.

```cpp
if (!uplink_host_session_open()) {
    // host로 보낼 수 없더라도 MCP RX FIFO는 계속 비운다.
    while (budget-- > 0 && mcp_has_rx()) {
        read_mcp_frame_and_discard();
        host_absent_rx_discard_total[bus]++;
    }
    service_mcp2515_status_after_drain(false);
    return;
}
```

정책:

```text
- CDC closed 중에는 frame payload를 저장하지 않는다.
- 대신 bus별 host_absent_rx_discard_total 누적
- host_absent_fifo_overflow_total 누적
- host_absent_mcp_spi_error_total 누적
- 다음 CDC open 때 compact summary event/BOARD_HEALTH로 보고
```

VSM 표시 예시:

```text
Capture state: incomplete
Reason: USB CDC session closed during capture
Close duration: 12,340 ms
Host-absent CAN frames discarded by CSM: 18,204
MCP FIFO overflow during host absent: 0
Vehicle bus impact evidence: no TX, no mode violation, no reset reported
```

---

### 시나리오 E — USB 재연결

#### 사용자의 실제 행동

```text
1. USB가 빠진 뒤 다시 꽂음
2. VSM 또는 Core가 재연결 시도
3. CSM이 CAPABILITY/BOARD_HEALTH 재송신
```

#### 제품 기대 동작

```text
- CSM reset 없음
- MCP reset 없음
- listen-only 유지
- EventUsbCdcSessionClose 28이 다음 open 때 보고됨
- close duration과 host absent discard summary가 보고됨
- VSM은 새로운 session을 이전 capture와 섞지 않음
```

#### 현재 구조의 객관 판정

CSM은 close duration을 다음 open 때 event 28로 보고한다. 이것은 맞다. 그러나 “host absent discard summary”는 아직 부족하다.

#### 요구 개선

CSM event 추가:

```text
29 USB_CDC_DTR_CHANGE
30 USB_HOST_ABSENT_CAN_DISCARD_SUMMARY
31 MCP_PASSIVE_MODE_READBACK
32 MCP_PASSIVE_MODE_VIOLATION
33 MCP_TXREQ_VIOLATION
34 TRANSCEIVER_SAFE_STATE_CHANGED
35 USB_POWER_OR_RESET_SUSPECTED
```

주의:

- event가 많아져 CAN truth를 밀어내면 안 된다.
- host absent 중에는 per-frame event 금지.
- summary만 다음 open 때 보낸다.

---

### 시나리오 F — Debug Tap 켜기

#### 사용자의 실제 행동

```text
1. VSM에서 GW켜기 또는 debug 관련 버튼 클릭
2. 사용자는 예전 COM gateway를 떠올릴 수 있음
3. 최신 VSM에서는 vsm-debug-tap.exe가 Core IPC에 붙음
```

#### 제품 기대 동작

```text
- Debug Tap은 COM/USB를 열지 않음
- Debug Tap은 host TX/control을 보내지 않음
- Debug Tap이 죽어도 Core capture에 영향 없음
- Debug Tap disconnect는 Core IPC lifecycle 문제로 표시
```

#### 현재 구조의 객관 판정

`vsm-debug-tap.exe`는 Core IPC client로 view snapshot을 읽는 구조다. COM-owning gateway가 아니다. 최신 방향은 맞다.

#### 요구 개선

UI 문구 변경:

```text
기존: GW 켜기
수정: Debug Tap 켜기 (Core IPC 기록, COM 미사용)
```

Trace 예시:

```json
{"event":"ipc_connected","server_name":"vsm-core-..."}
{"event":"view_changed","view_name":"transport_summary","view_seq":"152"}
{"event":"request_timeout","view_name":"capture_progress"}
{"event":"ipc_disconnected","capture_impact":"none","com_owner":"core"}
```

---

### 시나리오 G — bus1이 안 보임

#### 사용자의 실제 행동

```text
1. 차량은 2-bus 기대
2. VSM live view에 bus0만 표시
3. 사용자는 VSM이 bus1을 버렸는지 의심
```

#### 제품 기대 동작

VSM은 다음 셋을 구분해야 한다.

```text
case A: CSM capability bus_count<2
→ CSM이 bus1을 광고하지 않음

case B: CSM capability bus_count=2, bus1 rx_supported=1, bus1 frame=0
→ bus1 active but no traffic

case C: vehicle profile expects 2 buses, CSM capability bus_count<2
→ current CSM firmware/profile does not satisfy vehicle 2-bus requirement
```

#### 현재 구조의 객관 판정

현재 passive env는 `BOARD_ENABLE_BUILTIN_CAN_RX=0`이므로 bus1을 내지 않는 것이 자연스럽다. VSM 버그로 보기 어렵다.

#### 요구 개선

CSM:

```text
새 env 필요:
portenta_h7_m7_mid_mcp2515_j4_dual_csm_passive_2bus_rx
```

조건:

```text
bus0: MCP2515 listen-only
bus1: builtin CAN RX-only 또는 hardware-silent verified
host downlink: 0
host TX: 0
control path: 0
bus0 tx/control: false
bus1 tx/control: false
```

VSM:

```text
Vehicle expected buses: 2
CSM advertised buses: 1
Result: Incomplete vehicle profile, not VSM receive loss
```

---

### 시나리오 H — Full Instrumented firmware를 실차에서 잘못 사용

#### 제품 기대 동작

실차 passive acceptance에서는 즉시 차단되어야 한다.

#### 요구 개선

CSM:

```text
- passive product와 full instrumented를 USB PID 또는 firmware profile로 확실히 분리
- full instrumented는 lab-only banner/capability 강제
```

VSM:

```text
if firmware_profile == full_instrumented:
    field_passive_ready = false
    show "LAB-ONLY ACTIVE-CAPABLE FIRMWARE"
    block capture acceptance as passive evidence
```

---

## 3. 최종 CSM 플랜

### 3.1 CSM 아키텍처 재정의

CSM을 다음 두 영역으로 강제 분리한다.

```text
CSM
├─ Passive CAN Front-End
│  ├─ MCP2515 listen-only
│  ├─ transceiver silent/off/RX-safe
│  ├─ TXD inhibit / TXREQ guard
│  ├─ MCP mode readback
│  ├─ host-absent drain
│  └─ passive violation latch
│
└─ CDC Evidence Uplink
   ├─ typed stream
   ├─ CAPABILITY
   ├─ BOARD_HEALTH
   ├─ USB session open/close events
   └─ compact diagnostic summaries
```

절대 규칙:

```text
CDC open/close/DTR/re-enumeration must not change CAN front-end state.
```

---

### 3.2 CSM P0 구현 항목

#### P0-1. Host Absent Drain Mode

목표:

```text
USB가 닫혀도 MCP RX FIFO를 계속 비워서 차량 CAN front-end가 안정적으로 유지되게 한다.
```

구현 예시:

```cpp
static void service_can_frontend_when_host_absent(int budget) {
#if BOARD_ENABLE_MCP2515
  if (mcp2515 == nullptr || !can_backend_ok) return;

  uint32_t start_us = micros();
  while (budget-- > 0) {
    if (micros() - start_us > BOARD_MCP2515_RX_DRAIN_TIME_BUDGET_US) {
      host_absent_drain_budget_hit_total++;
      break;
    }

    struct can_frame msg;
    const MCP2515::ERROR err = mcp2515->readMessage(&msg);
    if (err == MCP2515::ERROR_NOMSG) break;

    if (err == MCP2515::ERROR_OK) {
      host_absent_rx_discard_total[BOARD_MCP2515_BUS_ID]++;
      continue;
    }

    host_absent_mcp_error_total++;
    break;
  }

  service_mcp2515_status_after_drain(false);
#endif
}
```

loop 적용 예시:

```cpp
if (!uplink_host_session_open()) {
    service_can_frontend_when_host_absent(BOARD_MCP2515_LOOP_ENTRY_DRAIN_BUDGET);
    service_passive_mode_readback_guard();
    update_safety_state();
    kick_runtime_watchdog();
    return;
}
```

주의:

- host absent 중에는 per-frame typed record를 만들지 않는다.
- host absent 중에는 per-frame JSON/event를 만들지 않는다.
- 다음 open 때 summary만 보낸다.

---

#### P0-2. Passive Mode Readback Guard

목표:

```text
MCP2515가 listen-only에서 벗어나는 순간 제품 실패로 잡는다.
```

검사 항목:

```text
- CANCTRL mode bits == listen-only
- TXB0CTRL/TXB1CTRL/TXB2CTRL TXREQ == 0
- unexpected TX interrupt == 0
- passive_violation_latch == 0
```

구현 예시:

```cpp
static void service_passive_mode_readback_guard() {
#if BOARD_CSM_PROFILE_PASSIVE_PRODUCT && BOARD_ENABLE_MCP2515
  const uint8_t canctrl = mcp2515_raw_read_register(kMcpRegCanctrl);
  const uint8_t txb0 = mcp2515_raw_read_register(kMcpRegTxb0ctrl);
  const uint8_t txb1 = mcp2515_raw_read_register(0x40);
  const uint8_t txb2 = mcp2515_raw_read_register(0x50);

  if (canctrl == 0xFFu) {
    passive_spi_fault_total++;
    emit_board_event(EventMcp2515SpiFault, 0xFF, passive_spi_fault_total);
    return;
  }

  const bool listen_only = (canctrl & 0xE0u) == 0x60u; // MCP2515 listen-only mode bits
  if (!listen_only) {
    passive_violation_latch |= kPassiveViolationMcpMode;
    emit_board_event(EventMcpPassiveModeViolation, canctrl, passive_violation_latch);
  }

  if ((txb0 | txb1 | txb2) & kMcpTxbTxreq) {
    passive_violation_latch |= kPassiveViolationTxReq;
    emit_board_event(EventMcpTxReqViolation,
                     static_cast<uint16_t>((txb0 << 8) | txb1),
                     passive_violation_latch);
  }
#endif
}
```

---

#### P0-3. Transceiver Silent/Off Contract

현재 MCP2515 listen-only는 controller-level safety다. 제품급은 transceiver-level safety가 필요하다.

현재 TJA1050 계열 모듈처럼 silent/off 제어가 약하면 다음 중 하나가 필요하다.

```text
A. TJA1051/TJA1057/TCAN1044 계열로 변경하여 Silent/STB/EN pin 제어
B. TXD physical gate 추가
C. transceiver EN/S pin을 passive default safe state로 pull-up/pull-down
D. USB reset 중에도 transceiver가 silent/off로 유지되는 하드웨어 default 설계
```

정책:

```text
passive product boot default:
  transceiver = silent or RX-safe
  TXD = recessive/fail-safe
  MCU firmware가 죽어도 CAN dominant 불가
```

---

#### P0-4. USB Power/Ground Fault Containment

제품급 테스트에서 가장 위험한 것은 코드가 아니라 USB 물리 연결이다.

검토 항목:

```text
- USB VBUS가 CSM/CAN side를 back-power하지 않는가
- PC GND 연결 순간 CAN reference가 흔들리지 않는가
- USB shield/GND 연결 정책이 명확한가
- MCP2515 VCC가 USB plug/unplug 때 dip/spike 없는가
- transceiver VCC/VIO가 USB plug/unplug 때 흔들리지 않는가
- CANH/CANL에 dominant pulse가 보이지 않는가
```

하드웨어 개선 후보:

```text
- USB isolator
- isolated CAN transceiver
- isolated DC/DC
- ideal diode / load switch
- VBUS inrush limiting
- TVS / common-mode choke
- TXD series resistor + pull-up
- transceiver silent pin hardware default
```

---

#### P0-5. Passive 2-Bus Product Profile

정정: 현재 제품 passive profile은 항상 2-bus RX여야 한다. 2개 미만 RX bus를 광고하는 build/upload는 실차 제품이 아니며, VSM은 vehicle profile mismatch 또는 passive capability rejection으로 표시해야 한다.

```ini
[env:portenta_h7_m7_mid_mcp2515_j4_dual_csm_passive_2bus_rx]
extends = env:portenta_h7_m7
build_src_filter =
  ${env:portenta_h7_m7.build_src_filter}
  -<board/HostDownlinkParser.cpp>
build_flags =
  -D BOARD_CSM_PROFILE_PASSIVE_PRODUCT=1
  -D BOARD_HW_PROFILE_MID_MCP2515=1
  -D BOARD_ENABLE_MCP2515=1
  -D BOARD_ENABLE_BUILTIN_CAN_RX=1
  -D BOARD_ENABLE_HOST_CAN_TX=0
  -D BOARD_ENABLE_HOST_CAN_TX_BUILTIN=0
  -D BOARD_ENABLE_HOST_CAN_TX_MCP2515=0
  -D BOARD_ENABLE_HOST_DOWNLINK=0
  -D BOARD_MCP2515_LISTEN_ONLY_BY_DEFAULT=1
  -D BOARD_BUILTIN_CAN_CONTROL_TX_ALLOWED=0
  -D BOARD_MCP2515_CONTROL_TX_ALLOWED=0
  -D BOARD_USB_CDC_RECONNECT_RESET_MS=0
```

주의:

- builtin CAN이 진짜 RX-only/listen-only가 가능한지 별도 검증 필요.
- internal CAN transceiver가 ACK를 내면 “silent passive”가 아니다.
- 2-bus passive product는 hardware safety case가 따로 필요하다.

---

## 4. 최종 VSM 플랜

### 4.1 VSM 역할 재정의

VSM은 CAN bus safety를 직접 보장하지 않는다. VSM은 다음을 해야 한다.

```text
- CSM CAPABILITY/BOARD_HEALTH/evidence를 검증한다.
- 사용자가 profile/bus/session/capture 상태를 오해하지 않게 표시한다.
- debug tap은 COM을 열지 않고 Core IPC만 구독한다.
- incomplete capture와 verified capture를 분리한다.
- passive acceptance 실패 시 field-ready를 차단한다.
```

---

### 4.2 VSM P0 구현 항목

#### P0-1. Vehicle Profile Requirement 도입

사용자가 “이 차량은 2-bus가 정상”이라고 정할 수 있어야 한다.

예시 파일:

```json
{
  "vehicle_model": "airport_shuttle_v1",
  "expected_buses": [
    {"bus": 0, "name": "drive", "required": true, "expected_bitrate": 500000},
    {"bus": 1, "name": "system", "required": true, "expected_bitrate": 500000}
  ],
  "required_passive_state": "verified_or_candidate_with_bench_override"
}
```

UI 판정:

```text
Expected bus count: 2
CSM advertised bus count: 1
Result: CSM firmware/profile does not satisfy selected vehicle profile
```

---

#### P0-2. Capability mismatch UI

표시 예시:

```text
CSM Capability
- firmware: passive_product
- vehicle impact: configured_passive, not verified
- passive acceptance: false
- host command RX: false
- control path: false
- downlink records: 0
- bus0: MCP2515, listen-only, RX supported
- bus1: not advertised

Verdict:
- VSM receive bug: no evidence
- CSM product profile mismatch: yes, selected vehicle expects bus1
```

---

#### P0-3. USB Lifecycle Timeline

CSM event 27/28를 timeline에 표시한다.

예시:

```text
11:03:41.210 USB_CDC_SESSION_CLOSE duration=14532ms
11:03:43.011 USB_CDC_SESSION_OPEN dtr_required=1 dtr_session_only=1
11:03:43.020 CAPABILITY received profile=passive_product
11:03:43.030 BOARD_HEALTH received passive_violation=0
11:03:43.055 MCP_STATUS canctrl=0x60 eflg=0x00 canintf=0x00
```

USB 탈착 후 MCP error가 보이면:

```text
Diagnosis:
CSM/MCP/CAN front-end instability after USB lifecycle event.
This is not a VSM parser/storage issue.
```

---

#### P0-4. .part Capture Product Handling

`.part` 세션은 절대 정상 log처럼 보이면 안 된다.

표시 예시:

```text
Capture status: incomplete
Finalize marker: missing
Stream parse: clean SOF/CRC/seq or corrupted
Index state: missing/partial/ok
Likely cause: USB disconnect or Core process interrupted
Passive impact evidence: requires CSM events around disconnect
```

리포트 산출:

```text
capture_recovery_report.json
- stream_bytes
- parsed_records
- last_good_seq
- typed_seq_gap_count
- usb_session_events
- board_health_before_disconnect
- board_health_after_reconnect
- mcp_error_delta
- fifo_overflow_delta
- verdict
```

---

#### P0-5. Debug Tap UI 문구 변경

```text
기존: GW 켜기
수정: Debug Tap 켜기 (Core IPC 기록, COM 미사용)
```

상태 표시:

```text
Debug Tap:
- COM ownership: no
- Host TX/control: no
- Core state mutation: no
- Capture impact: none
- Trace file: debug_tap_trace.jsonl
```

---

#### P0-6. Passive Acceptance Gate

VSM은 다음 조건을 모두 만족하지 않으면 “verified passive”를 표시하면 안 된다.

```text
firmware_profile == passive_product
host_command_rx == false
control_path == false
supported_downlink_records == 0
host_tx_queue_size == 0
all advertised buses: tx_supported == false
all advertised buses: control_tx_allowed == false
passive_violation_latch == 0
vehicle_impact_state == verified_passive OR explicit bench override
selected vehicle bus requirements satisfied
```

---

## 5. 통합 Acceptance Test

### 5.1 USB plug/unplug 무영향 테스트

조건:

```text
- 차량 CAN 또는 CAN simulator + PCAN/Kvaser reference analyzer 병렬 연결
- CSM passive firmware
- VSM passive product
- USB plug/unplug 100회
- VSM process kill/restart 50회
- Debug Tap on/off 50회
```

PASS 기준:

```text
- CAN analyzer 기준 error frame 증가 0
- vehicle DTC 증가 0
- CSM CAN TX count 0
- MCP normal mode violation 0
- TXREQ violation 0
- passive_violation_latch 0
- USB reconnect reset 0
- MCU reset count 0
- CANH/CANL dominant glitch 0
- MCP FIFO overflow 0 또는 host-absent discard 정책으로 원인 명확
- VSM capture incomplete/finalized 판정 정확
```

---

### 5.2 Host Absent Drain 테스트

조건:

```text
- CAN frame 500kbps live
- USB cable unplug 30초
- CSM 전원은 유지
- USB 재연결
```

PASS 기준:

```text
- USB 닫힌 동안 MCP FIFO overflow 0
- host_absent_rx_discard_total 증가
- CSM reset 0
- MCP mode violation 0
- 다음 open 때 close duration과 discard summary 보고
```

---

### 5.3 2-Bus Vehicle Profile 테스트

조건:

```text
- VSM vehicle profile expected bus_count=2
- CSM passive_1bus_mcp firmware
```

PASS 기준:

```text
VSM must show:
- CSM advertised buses: 1
- Vehicle expected buses: 2
- Result: firmware/profile mismatch
- Not: VSM bus1 receive bug
```

조건 2:

```text
- CSM passive_2bus_rx firmware
```

PASS 기준:

```text
- bus0 rx_supported=1 tx/control=0
- bus1 rx_supported=1 tx/control=0
- bus0/bus1 frames displayed separately
- missing traffic and missing capability are distinguished
```

---

## 6. Codex 실행 지시문

```text
현재 목표는 CDC 제거가 아니다.
CDC를 유지한 상태에서 CSM을 차량 CAN 비간섭형 passive probe로 제품화한다.

기준 커밋:
- VSM: 11e9470 Productize passive debug tap plane
- CSM: a65b582 Report USB CDC session close events

최종 단일 결론:
CSM은 Passive CAN Front-End와 CDC Evidence Uplink를 분리해야 한다.
CDC open/close/DTR/re-enumeration은 MCP2515, transceiver, reset, TX gate에 영향을 주면 안 된다.
VSM은 이 evidence를 검증하고 오해 없이 표시하는 product workstation이어야 한다.

CSM P0:
1. Host Absent Drain Mode 구현
2. Passive Mode Readback Guard 구현
3. TXREQ violation / MCP mode violation event 추가
4. USB close/open summary에 host_absent_rx_discard_total, fifo_overflow_delta, mcp_error_delta 포함
5. transceiver silent/off/TXD inhibit 하드웨어 계약 문서화
6. passive_2bus_rx profile은 별도 env로 만들고, full instrumented를 실차 passive 용도로 쓰지 말 것

VSM P0:
1. Vehicle profile expected bus requirement 추가
2. capability bus_count mismatch를 VSM receive bug가 아니라 CSM profile mismatch로 표시
3. USB lifecycle event 27/28 timeline 표시
4. .part capture recovery report 생성
5. GW켜기 문구를 Debug Tap 켜기(Core IPC, COM 미사용)로 변경
6. passive acceptance gate를 capability/health/profile/bus requirement 기준으로 차단

주의:
- debug/event를 per-frame으로 늘리지 말 것.
- host absent 중에는 frame payload를 저장하려 하지 말 것.
- 제품 PASS는 코드 수정만으로 주장하지 말고 plug/unplug acceptance test로 판정할 것.
```

---

## 7. 최종 판정

현재 코덱스 분석의 방향은 맞다. 그러나 더 상위 제품 결론은 다음 문장으로 고정해야 한다.

```text
CSM은 CDC를 유지하되, CDC와 CAN front-end를 fault containment boundary로 분리한
차량 CAN 비간섭형 passive probe가 되어야 한다.
VSM은 이 probe가 실제로 passive인지 CAPABILITY/BOARD_HEALTH/USB lifecycle/capture evidence로
검증하고, 사용자가 bus/profile/session/capture 상태를 오해하지 않게 표시해야 한다.
```

이 결론을 만족하지 못하면 PCAN/Kvaser급 제품 방향이 아니다.
