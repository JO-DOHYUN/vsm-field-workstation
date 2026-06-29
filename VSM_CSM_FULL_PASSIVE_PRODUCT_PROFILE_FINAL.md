# VSM/CSM Full Instrumented & Passive Product Profile 최종 제품형 아키텍처 기준

## 0. 문서 목적

이 문서는 VSM/CSM을 PC 버전과 Android 확장까지 고려한 **산업 완성형 제품 구조**로 정의하기 위한 최종 기준이다.

핵심 목표는 단순히 로그를 잘 저장하는 것이 아니다.

> VSM UI, VSM Core, Debug, Gateway, Android, PC, CSM 펌웨어, USB 연결/해제, capture start/stop, replay/debug attach가 어떤 타이밍에 붙고 떨어져도 차량 CAN에는 영향을 주지 않는 구조를 제품 계약으로 보장한다.

이 문서는 다음을 명확히 분리한다.

1. 현재 코드/구조의 문제점
2. 최종 제품형 구조
3. `Full Instrumented Profile`과 `Passive Product Profile`의 정확한 차이
4. VSM과 CSM의 경계 책임
5. PC/Android 공통 모듈 구조
6. CSM/VSM capability handshake
7. 최종 acceptance 기준

---

## 1. 최종 결론

최종 구조는 **하드웨어를 무조건 두 개로 나누는 것**이 아니다.

정확한 표현은 다음이다.

> VSM/CSM은 하나의 공통 데이터 코어를 공유하되, 실행 제품 프로파일을 `Full Instrumented Profile`과 `Passive Product Profile`로 분리한다.  
> 두 프로파일은 UI 옵션 수준이 아니라, VSM runtime profile과 CSM firmware capability가 서로 매칭되어야 성립하는 제품 계약이다.

즉 최종 구조는 다음이다.

```text
Shared Data Core
  ├─ capture.stream/index
  ├─ typed parser
  ├─ materialized views
  ├─ analysis
  ├─ replay
  └─ diagnostics schema

Product Profiles
  ├─ Full Instrumented Profile
  │   └─ 개발·검증·벤치·계측용 전체 기능 패키지
  │
  └─ Passive Product Profile
      └─ 실차·현장 기본 제품 모드, 차량 영향 가능 경로 hard reject
```

`Full`은 “디버그 모드”가 아니라 **벤치/개발/검증용 전체 계측 프로파일**이다.  
`Passive`는 “기능 제한 모드”가 아니라 **실차 기본 제품 프로파일**이다.

---

## 2. 산업형 기준 검토

### 2.1 PCAN/Kvaser 계열의 핵심 원리

산업형 CAN 장비는 앱이 기능을 많이 갖고 있더라도, 차량 CAN에 붙는 채널의 **bus participation mode**를 명확히 나눈다.

Kvaser silent mode는 CAN bus에 data frame, error frame, ACK slot을 송신하지 않고 CAN bus를 dominant 상태로 구동하지 않는다고 설명한다.  
PCAN ListenOnly 역시 CAN controller가 active event, 즉 송신에 참여하지 않는 passive monitor 모드로 정의된다.

따라서 산업형 기준의 핵심은 다음이다.

```text
앱 기능이 많냐 적냐가 아니라,
vehicle-facing channel이 listen-only/silent/passive인지,
host TX/control/debug write가 해당 채널로 도달 가능한지,
그 경계가 코드와 capability에서 강제되는지이다.
```

### 2.2 Android 확장 기준

Android는 USB Host API에서 `UsbManager`, `UsbDevice`, `UsbInterface`, `UsbEndpoint`, `UsbDeviceConnection`처럼 장치/인터페이스/엔드포인트/연결을 계층으로 나눈다.

따라서 Android판도 UI Activity가 차량 링크를 직접 만지면 안 된다.

Android 최종 구조는 다음이어야 한다.

```text
Android Activity UI
  ↓ view query only
Foreground Core Service
  ↓ profile/capability contract
Android USB Adapter
  ↓ passive CSM link
CSM
```

PC판과 Android판은 UI와 platform adapter만 다르고, shared core/profile/capability/data contract는 같아야 한다.

---

## 3. 현재 코드/구조의 문제점

아래 문제는 `c773468` 기준 코드 구조에서 확인되는 현재 위험 경계다.

### 3.1 Serial open이 passive monitor가 아니다

현재 `SerialDrainRuntime::startSerial()`은 포트를 `QIODevice::ReadWrite`로 열고, DTR/RTS를 true로 설정한다.

```text
m_serial->open(QIODevice::ReadWrite)
m_serial->setDataTerminalReady(true)
m_serial->setRequestToSend(true)
```

문제:

```text
- Passive monitor가 아니라 active USB CDC endpoint로 동작한다.
- DTR/RTS edge가 MCU reset, CDC state change, bootloader 진입, firmware host-present mode를 유발할 수 있다.
- USB 연결/해제 자체가 CSM 상태와 CAN transceiver 상태를 바꿀 수 있다.
```

### 3.2 Stop/disconnect도 DTR/RTS edge를 만든다

현재 stop 시 serial clear 후 RTS/DTR을 false로 내리고 close한다.

문제:

```text
- connect 순간뿐 아니라 disconnect, core restart, 앱 종료, port error, GW 전환 시에도 제어선 edge가 생긴다.
- 실차 system CAN 주기 에러가 connect/disconnect 타이밍과 일치할 수 있다.
```

### 3.3 Host TX path가 살아 있다

현재 VSM core에는 다음 active 경로가 존재한다.

```text
CoreProcessClientRuntime::sendHostFrame()
CoreIpcServerRuntime::host_frame
SerialDrainRuntime::sendHostFrame()
SerialDrainRuntime::drainHostTxQueue()
```

문제:

```text
- Passive 실차 모드에서 UI 버튼만 숨기는 것으로는 부족하다.
- IPC 서버 단계에서 host_frame이 성공할 수 있으면 제품형 passive contract 위반이다.
- 같은 serial/TCP device를 read와 write가 공유하므로 backpressure나 command path가 차량 링크와 결합될 수 있다.
```

### 3.4 Control cycle path가 살아 있다

현재 control cycle 경로에는 heartbeat, lease renew, CAN TX request 생성 경로가 있다.

문제:

```text
- Passive Product Profile에서는 control_cycle message 자체가 hard reject되어야 한다.
- active/test profile에서도 unlock, lease, audit, timeout 없이는 허용되면 안 된다.
```

### 3.5 Debug Gateway는 현재 observer가 아니라 route replacement다

현재 `vsm_debug_gateway.py`는 serial port를 직접 열고, serial에서 읽은 데이터를 disk/TCP로 전달한다. 동시에 TCP에서 받은 데이터를 serial로 다시 write하는 경로가 존재한다.

문제:

```text
현재 구조:
COM → Debug Gateway → TCP → VSM Core

위험:
- Debug Gateway가 COM owner가 된다.
- Debug on/off가 serial open/close edge를 만든다.
- TCP→serial write가 가능하다.
- Passive Product Profile의 debug sidecar가 아니다.
```

따라서 현재 debug gateway는 다음으로 분류해야 한다.

```text
현재 vsm_debug_gateway.py
= Full Instrumented / Lab Gateway Tool
≠ Passive Product Debug Sidecar
```

### 3.6 VSM drain/backpressure는 CSM uplink에 역압을 줄 수 있다

현재 VSM drain 구조는 이전보다 개선되어 있지만, 제품형 passive 보장과는 별개다.

실제 로그 분석상 다음 계열의 보드 카운터가 관측되었다.

```text
canSegmentEnqueueFailTotal
uplinkPoolAllocFailTotal
serialBackpressureTotal
serialTxHighWaterBytes
```

해석:

```text
- PC/VSM이 USB IN을 늦게 drain하면 CSM USB CDC TX가 막힐 수 있다.
- CSM uplink descriptor/pool 반환 지연이 CAN_RX_SEGMENT enqueue fail로 이어질 수 있다.
- 만약 CSM CAN RX task와 uplink/debug/control pool이 결합되어 있으면, VSM/USB 상태가 차량 CAN 처리 시간에 영향을 줄 수 있다.
```

### 3.7 `Full`과 `Passive`의 현재 경계가 명시적이지 않다

현재 구조는 2+1 프로세스 분리와 live_latest 정리는 상당히 진행되었지만, 제품 프로파일 관점의 다음 계약이 아직 부족하다.

```text
- VSM runtime profile 없음
- CSM firmware capability manifest 없음
- VSM profile과 CSM capability 매칭 없음
- Passive 상태에서 host_frame/control_cycle/debug_gateway hard reject 계약 없음
- vehicle_impact_capability 표시 없음
```

---

## 4. 최종 제품 프로파일 정의

## 4.1 Full Instrumented Profile

### 목적

개발, 검증, 벤치, 장애 재현, HIL, deep diagnostics용 전체 계측 프로파일이다.

### 허용 기능

```text
- capture.stream/index
- decoded tail
- live_latest
- analysis
- graph bucket
- diagnostics
- replay
- debug trace
- lab gateway
- host TX
- control cycle
- heartbeat/lease test
- audit log
```

### 반드시 표시해야 하는 상태

```text
vehicle_impact_capability = possible 또는 active
profile = full_instrumented
bench_or_lab_only = true
passive_acceptance_allowed = false
```

### Full Profile의 규칙

Full은 실차 기본 모드가 아니다.

```text
- 차량 영향 가능 경로가 존재할 수 있다.
- 따라서 실차 passive PASS를 주장할 수 없다.
- host TX/control은 unlock/audit/timeout 하에서만 허용한다.
- Debug Gateway는 lab gateway로만 허용한다.
```

---

## 4.2 Passive Product Profile

### 목적

실차, 현장, 고객/운영 환경에서 기본으로 사용하는 제품 프로파일이다.

### 최상위 계약

```text
vehicle_impact_capability = impossible
```

단, 이 값은 UI 표시만으로 선언할 수 없다. VSM runtime profile과 CSM firmware capability가 모두 조건을 만족해야 한다.

### Passive Product Profile 필수 조건

```text
CAN:
- CAN controller listen-only 또는 silent
- CAN TX 불가
- ACK 불가 또는 제품 요구에 맞는 non-disturbing passive mode
- error frame 생성 불가
- reset 중 transceiver safe

USB/Host:
- serial open mode = read_only
- DTR policy = no_touch
- RTS policy = no_touch
- USB open/close로 CSM mode 변화 금지
- USB backpressure가 CAN RX task에 역류 금지

VSM/Core:
- host_frame IPC hard reject
- control_cycle IPC hard reject
- Debug Gateway start hard reject
- TCP-to-serial write 경로 없음
- active/test module 미생성 또는 disabled-by-profile
- UI 버튼 비활성화가 아니라 core boundary에서 불가능

Debug:
- passive debug는 COM을 소유하지 않는다.
- passive debug는 core/capture/view에 attach하는 sidecar만 허용한다.
- debug attach/detach가 vehicle-facing path를 변경하면 안 된다.
```

---

## 5. VSM 최종 제품형 구조

### 5.1 VSM 최종 모듈

```text
vsm/shared-core/
  protocol/
  capture/
  parser/
  views/
  analysis/
  diagnostics/
  replay/
  profiles/

vsm/platform-pc/
  qt_ui/
  core_process_launcher/
  serial_adapter/

vsm/platform-android/
  activity_ui/
  foreground_core_service/
  android_usb_adapter/

vsm/vehicle-link/
  csm_capability/
  passive_contract/
  transport_policy/

vsm/debug-tools/
  sidecar_attach/
  artifact_reader/
  lab_gateway/

vsm/active-test/
  host_tx/
  control_cycle/
  unlock/
  audit/
  lease/
```

### 5.2 VSM 공통 Core 책임

공통 Core는 Full/Passive 양쪽이 공유한다.

```text
- typed protocol parser
- capture.stream/index writer
- materialized view store
- live_latest
- decoded_tail
- analysis snapshot
- graph bucket
- replay
- diagnostics schema
- profile/capability verifier
```

공통 Core는 차량에 직접 쓰기 기능을 포함하지 않는다.  
쓰기 기능은 `active-test` module에 격리한다.

### 5.3 VSM Passive Product Profile 책임

```text
- serial open policy read_only/no-touch
- host TX IPC reject
- control IPC reject
- debug gateway reject
- VSM profile manifest expose
- CSM capability manifest verify
- vehicle_impact_state 계산
- UI에 pass/fail/guarded 표시
```

### 5.4 VSM Full Instrumented Profile 책임

```text
- debug trace
- lab gateway
- host TX
- control cycle
- lease/unlock
- audit
- HIL diagnostics
```

단 Full은 항상 다음을 표시한다.

```text
vehicle_impact_capability = possible
bench/test only
passive acceptance forbidden
```

---

## 6. CSM 최종 제품형 구조

### 6.1 CSM 공통 모듈

```text
csm/common/
  typed_protocol/
  capture_record_encoder/
  board_health/
  capability_manifest/
  monotonic_time/
  crc/
```

### 6.2 CSM vehicle-facing passive core

```text
csm/vehicle_can/
  can_controller_config/
  listen_only_or_silent/
  rx_isr/
  rx_ring/
  transceiver_safe_state/
```

책임:

```text
- CAN RX ISR/task는 fixed ring append 후 즉시 return
- malloc 금지
- USB 상태 조회 금지
- blocking 금지
- debug event 생성 금지
- host command 처리 금지
```

### 6.3 CSM telemetry/uplink plane

```text
csm/uplink/
  typed_stream_encoder/
  usb_cdc_tx/
  wifi_or_bt_tx_optional/
  uplink_drop_counter/
  backpressure_policy/
```

책임:

```text
- CAN RX ring에서 읽어 host로 telemetry 전송
- USB backpressure 발생 시 uplink만 drop 또는 invalid 기록
- CAN RX task에 역압 전파 금지
- uplink pool은 CAN RX pool과 분리
```

### 6.4 CSM debug plane

```text
csm/debug/
  low_priority_trace/
  snapshot_counter/
  diagnostic_event_ring/
```

책임:

```text
- 기본 off 또는 bounded
- CAN RX task와 pool 공유 금지
- debug 폭주가 uplink/can_rx를 막지 못함
```

### 6.5 CSM active-test plane

```text
csm/active_test/
  host_command_rx/
  can_tx/
  heartbeat/
  control_session/
  lease/
  unlock/
  audit/
```

책임:

```text
- Passive Product Profile에서는 컴파일 제외 또는 capability false
- Full Instrumented Profile에서만 unlock/audit/timeout 조건으로 허용
```

---

## 7. VSM/CSM Capability Handshake

### 7.1 CSM capability manifest 예시

```json
{
  "device": "CSM",
  "firmware_profile": "passive_product",
  "firmware_build_id": "csm-passive-2026xxxx",
  "can": {
    "bus_mode": "listen_only",
    "tx_capability": false,
    "ack_capability": false,
    "error_frame_capability": false,
    "transceiver_reset_safe": true
  },
  "host_link": {
    "host_command_rx": false,
    "control_path": false,
    "usb_backpressure_isolated": true,
    "dtr_reset_sensitive": false
  },
  "debug": {
    "lab_gateway_allowed": false,
    "sidecar_attach_allowed": true
  }
}
```

### 7.2 VSM runtime profile manifest 예시

```json
{
  "app": "VSM",
  "runtime_profile": "passive_product",
  "platform": "pc",
  "serial": {
    "open_mode": "read_only",
    "dtr_policy": "no_touch",
    "rts_policy": "no_touch"
  },
  "host_tx": {
    "enabled": false,
    "ipc_hard_reject": true
  },
  "control": {
    "enabled": false,
    "ipc_hard_reject": true
  },
  "debug": {
    "lab_gateway_enabled": false,
    "sidecar_attach_allowed": true
  }
}
```

### 7.3 매칭 결과

```text
CSM passive + VSM passive
= vehicle_impact_state: impossible
= 실차 passive PASS 가능

CSM passive + VSM full
= vehicle_impact_state: guarded
= host_tx/control/gateway hard reject 없으면 FAIL

CSM active + VSM passive
= vehicle_impact_state: blocked 또는 not_passive
= 실차 passive PASS 불가

CSM active + VSM full
= vehicle_impact_state: active_possible
= bench/test only
```

---

## 8. 데이터 평면 분리

최종 구조는 모든 데이터를 한 경로로 섞지 않는다.

### 8.1 Vehicle-facing plane

```text
Vehicle CAN
  ↓
CSM CAN RX ISR/task
  ↓
CAN RX ring
```

계약:

```text
- 최고 우선순위
- host 상태와 독립
- debug/control/uplink와 pool 분리
- blocking 금지
```

### 8.2 Telemetry/capture plane

```text
CAN RX ring
  ↓
typed stream encoder
  ↓
VSM/Android Core
  ↓
capture.stream/index
```

계약:

```text
- host가 느리면 telemetry만 drop/invalid
- vehicle CAN timing 영향 금지
- capture invalid는 명시
```

### 8.3 View plane

```text
Core
  ├─ live_latest
  ├─ decoded_tail
  ├─ analysis_snapshot
  ├─ graph_bucket
  └─ diagnostics
```

계약:

```text
- UI는 view query만
- raw stream push 금지
- UI freeze가 capture/CAN RX에 영향 금지
```

### 8.4 Debug plane

```text
Core/capture/view
  ↓
debug sidecar attach
```

계약:

```text
- passive debug는 COM 소유 금지
- serial write 금지
- vehicle-facing path 변경 금지
```

### 8.5 Active-test plane

```text
UI/bench tool
  ↓ unlock/audit
host TX / control cycle
  ↓
CSM active-test path
  ↓
CAN TX
```

계약:

```text
- Full Instrumented Profile에서만 가능
- Passive에서는 IPC server 단계에서 hard reject
- audit/lease/timeout 필수
```

---

## 9. Debug Gateway 최종 분류

현재 `vsm_debug_gateway.py`는 최종 passive sidecar가 아니다.

최종 분류:

```text
vsm_debug_gateway.py
= Lab Gateway Tool
= Full Instrumented Profile 전용
= COM owner 가능
= TCP-to-serial write 가능
= vehicle_impact_capability possible
```

Passive Product Profile의 debug는 새로 정의해야 한다.

```text
Passive Debug Sidecar
= Core/capture/view attach only
= COM owner 아님
= serial write 없음
= CSM mode 변경 없음
= DTR/RTS 영향 없음
```

---

## 10. UI/Diagnostics 최종 표시 항목

VSM UI는 단순히 connected만 표시하면 안 된다.

필수 표시:

```text
runtime_profile
csm_firmware_profile
vehicle_impact_state
can_bus_mode
serial_open_mode
dtr_policy
rts_policy
host_tx_enabled
control_enabled
debug_mode
debug_gateway_allowed
sidecar_attach_allowed
capture_truth_valid
uplink_loss_count
can_rx_loss_count
profile_match_result
```

예시:

```text
Profile: Passive Product
CSM: Passive Product / listen-only
Vehicle Impact: IMPOSSIBLE
Serial: ReadOnly / DTR no-touch / RTS no-touch
Host TX: disabled / IPC hard reject
Control: disabled / IPC hard reject
Debug: sidecar attach only
Capture Truth: capture.stream/index valid
```

Full 예시:

```text
Profile: Full Instrumented
Vehicle Impact: POSSIBLE
Mode: Bench/Test only
Host TX: unlock required
Control: lease/audit enabled
Debug Gateway: lab tool enabled
Passive Acceptance: NOT ALLOWED
```

---

## 11. Codex 지시문

아래 지시문은 구현 우선순위가 아니라 **최종 제품형 구조를 만들기 위한 작업 지시**다.

```text
목표:
VSM/CSM을 Full Instrumented Profile과 Passive Product Profile로 분리하라.
기능 삭제가 아니라 capability/profile boundary를 제품급으로 강제하는 작업이다.

최종 구조:
1. shared-core는 capture.stream/index, typed parser, view store, analysis, replay, diagnostics를 공유한다.
2. profile_manifest를 추가하고 VSM runtime profile과 CSM firmware capability를 handshake한다.
3. Passive Product Profile에서는 차량 영향 가능 경로를 모두 hard reject한다.
4. Full Instrumented Profile에서는 debug/control/hostTX를 허용하되 vehicle_impact_capability=possible로 표시하고 bench/test 전용으로 audit한다.
5. 현재 vsm_debug_gateway.py는 passive debug가 아니라 lab gateway로 분류한다.
6. passive debug는 COM을 열지 않고 core view/capture artifact에 attach하는 sidecar로만 허용한다.

VSM 요구:
- profiles/passive_product.profile.json 추가
- profiles/full_instrumented.profile.json 추가
- CoreRuntimeProfile 타입 추가
- CsmCapabilityManifest 타입 추가
- ProfileMatchResult 타입 추가
- 연결 직후 CSM capability record를 파싱하여 VSM profile과 매칭
- mismatch면 UI에 vehicle_impact_state 표시
- Passive에서 host_frame IPC hard reject
- Passive에서 control_cycle IPC hard reject
- Passive에서 debug gateway start hard reject
- Passive에서 SerialDrainRuntime open_mode=ReadOnly
- Passive에서 DTR/RTS no-touch
- Passive에서 TCP-to-serial write path 없음
- Full에서만 Debug Gateway, Host TX, ControlCycle 사용 가능
- Full에서 사용 시 audit log 필수
- Settings/Overview에 현재 profile, CSM capability, vehicle_impact_capability 표시

CSM 요구:
- firmware_profile manifest를 typed capability record로 송신
- passive firmware/mode에서는 CAN controller listen-only 또는 silent
- passive에서는 host command RX write path disabled
- passive에서는 CAN TX/control/heartbeat path disabled
- CAN RX ring과 uplink/debug/control pool 분리
- USB backpressure가 CAN RX task에 영향 주지 않도록 설계
- reset 중 transceiver safe 보장 상태를 capability에 표시
- uplink loss와 CAN RX loss를 분리 counter로 표시

금지:
- UI 버튼만 숨기는 방식 금지
- passive인데 sendHostFrame/startControlCycle 함수가 성공하는 구조 금지
- passive인데 debug gateway가 serial port를 여는 구조 금지
- CSM capability 없이 passive PASS 표시 금지
- VSM 설정만 passive이고 CSM이 active-capable인데 PASS 표시 금지
- debug on/off가 vehicle-facing path를 바꾸는 구조 금지

테스트:
- Passive profile에서 host_frame IPC → rejected
- Passive profile에서 control_cycle IPC → rejected
- Passive profile에서 debug gateway start → rejected
- Passive profile에서 SerialDrainRuntime open mode = ReadOnly
- Passive profile에서 DTR/RTS no-touch
- Passive profile에서 serial write call count = 0
- CSM active manifest + VSM passive → vehicle_impact_state=blocked_or_guarded
- CSM passive manifest + VSM passive → vehicle_impact_state=impossible
- Full profile에서 hostTX/control 가능하지만 audit row 생성
- Full profile은 실차 passive acceptance 불가 표시
- Debug sidecar attach/detach가 COM open/close를 만들지 않음
```

---

## 12. Acceptance Criteria

### 12.1 Bench gate

```text
USB plug/unplug 중 CANH/CANL dominant glitch 0
VSM connect/disconnect 반복 중 CAN error frame 증가 0
Passive mode에서 serial write 호출 0
Passive mode에서 DTR/RTS edge 0
Passive mode에서 host_frame/control_cycle hard reject
Passive mode에서 debug gateway hard reject
USB backpressure 상태에서 CAN RX task latency 변화 없음
CSM capability 없이 passive PASS 표시 금지
```

### 12.2 실차 gate

```text
USB만 꽂고 빼기 반복: 차량 ECU error 0
VSM connect/disconnect 반복: 차량 ECU error 0
capture start/stop 반복: 차량 ECU error 0
passive debug sidecar attach/detach 반복: 차량 ECU error 0
30분 이상 dual bus capture: vehicle system CAN period error 0
capture.stream/index 무결성 PASS
uplink loss 발생 시 capture invalid 또는 telemetry loss로 명시
```

### 12.3 Android gate

```text
Android Activity 재생성/회전/백그라운드 전환 중 vehicle-facing path 변화 0
Foreground Core Service 유지 중 capture.stream/index 연속성 PASS
USB permission 재승인/재연결 중 DTR/RTS equivalent 영향 없음
Android debug attach는 core view/capture attach만 허용
Passive profile에서 Android USB OUT transfer 호출 0
```

---

## 13. 최종 표현 정리

문서, UI, 코드 주석에서 다음 표현을 사용한다.

```text
Full Instrumented Profile
= 개발·검증·벤치용 전체 계측 프로파일.
= vehicle impact capability possible.
= debug/control/hostTX가 있을 수 있으므로 실차 passive acceptance 불가.

Passive Product Profile
= 실차·현장 기본 제품 프로파일.
= vehicle impact capability impossible.
= VSM/CSM capability가 모두 맞을 때만 PASS.

Shared Core
= Full과 Passive가 공유하는 capture/parser/view/analysis/replay/diagnostics 코어.

Capability Handshake
= VSM runtime profile과 CSM firmware capability가 맞는지 검증하는 제품 계약.

Lab Gateway
= Full 전용 COM-owning debug gateway.
= Passive debug sidecar가 아님.

Passive Debug Sidecar
= COM을 열지 않고 core/capture/view에 attach하는 관찰자.
```

---

## 14. 최종 아키텍처 결정

최종 제품형 구조는 다음으로 확정한다.

```text
VSM/CSM은 하나의 공통 데이터 코어를 공유한다.
제품 실행은 Full Instrumented Profile과 Passive Product Profile로 분리한다.
Full은 개발·검증·벤치용 전체 계측 패키지다.
Passive는 실차·현장 기본 제품 프로파일이다.
두 프로파일은 기능 ON/OFF가 아니라 VSM runtime profile과 CSM firmware capability의 매칭 계약이다.
Passive에서 차량 영향 가능 경로는 UI가 아니라 Core/IPC/transport/firmware boundary에서 hard reject되어야 한다.
Debug는 Full에서는 lab gateway를 허용할 수 있지만, Passive에서는 sidecar attach만 허용한다.
Android 확장도 동일 shared core/profile/capability contract를 사용하고 platform adapter만 교체한다.
```

이 구조가 현재 요구에 대한 최종 상위구상이다.
