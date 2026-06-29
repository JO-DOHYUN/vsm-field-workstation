# VSM/CSM 차량 무영향 최종 아키텍처 검토 문서

**기준 코드:** `c773468 Finalize live latest cleanup and debug tap visibility`  
**작성 목적:** 현재 VSM/CSM 구조에서 차량 CAN에 영향을 줄 수 있는 모든 주요 경계를 객관적으로 정리하고, PCAN/Kvaser류 산업형 CAN 인터페이스 수준의 최종 완성 방향을 제시한다.

---

## 0. 최종 결론

현재 VSM은 UI/Core 분리, live latest view, capture stream/index, debug tap visibility 측면에서는 크게 개선되었다. 그러나 **“언제 어떤 PC/Android/UI/debug/core/GW가 붙어도 차량에 절대 영향 없음”**이라는 기준으로 보면 아직 완성형이 아니다.

핵심 이유는 단순하다.

현재 VSM/CSM은 아직 일부 구간에서 다음 성격을 가진다.

```text
PC/Android/VSM/Core/GW
  → USB CDC serial endpoint
  → CSM firmware uplink/control 경계
  → CAN transceiver
  → vehicle CAN
```

반면 PCAN/Kvaser류 산업형 CAN 인터페이스는 기본 철학이 다르다.

```text
PC application
  → vendor driver/API
  → dedicated CAN controller
  → listen-only/silent/normal mode boundary
  → isolated CAN transceiver
  → vehicle CAN
```

즉 앱이 느려지거나, UI가 멈추거나, debug가 붙거나, PC가 USB를 열고 닫아도 차량 CAN에 영향을 줄 가능성을 하드웨어/드라이버/모드 경계에서 막는다.

따라서 VSM/CSM의 최종 방향은 **“VSM 앱을 잘 고치는 것”이 아니라, CSM을 PCAN/Kvaser식 passive CAN tap 제품처럼 재정의하는 것**이다.

---

# Part 1. 현 문제점

## 1. 요구사항 정의 자체가 현재 구조보다 더 높다

사용자가 요구하는 기준은 다음이다.

```text
VSM PC 버전이든 Android 버전이든,
UI든 core든 debug든 gateway든,
어떤 타이밍에 연결/해제/실행/중단되어도,
차량 CAN에는 전기적·논리적·타이밍적 영향이 없어야 한다.
```

이 기준에서는 다음도 전부 실패 조건이다.

```text
- CAN dominant glitch
- ACK 참여로 인한 bus 상태 변화
- error frame 발생
- host TX frame 송신
- control lease/heartbeat 송신
- USB 연결에 따른 MCU reset
- DTR/RTS edge에 따른 firmware mode 변화
- USB CDC backpressure가 CAN RX task timing에 영향
- debug gateway on/off가 물리 COM 경로를 바꿈
- PC/Android UI 부하가 CSM uplink를 막아 CAN task를 지연
- GND/common-mode 변화로 CAN transceiver 상태 변화
```

따라서 이 요구는 단순 소프트웨어 안정화가 아니라 **vehicle-impact-free safety contract**이다.

---

## 2. 코드상 확정 취약점 1: Serial open이 passive가 아니다

현재 `SerialDrainRuntime::startSerial()`은 serial port를 `ReadWrite`로 열고, DTR/RTS를 true로 올린다.

```text
m_serial->open(QIODevice::ReadWrite)
m_serial->setDataTerminalReady(true)
m_serial->setRequestToSend(true)
```

검토 기준 코드에서 해당 동작이 확인된다.

### 문제점

이 동작은 USB CDC 장치에 대해 단순 수신 모니터가 아니다. 장치에 따라 다음을 유발할 수 있다.

```text
- MCU reset
- bootloader entry
- CDC session 활성화
- firmware host-present 상태 변경
- USB TX task 활성화
- DTR/RTS 기반 모드 전환
```

실차에서 “USB를 꽂는 순간 ADCU/system CAN 주기 에러가 뜬다”면 이 지점은 가장 직접적인 VSM 측 취약부위다.

### 현재 구조의 위험

```text
VSM connect
  → QSerialPort open(ReadWrite)
  → DTR/RTS asserted
  → CSM firmware/USB CDC 상태 변화 가능
  → CAN task/transceiver/reset 경계 변화 가능
  → vehicle CAN 영향 가능
```

---

## 3. 코드상 확정 취약점 2: disconnect/stop도 passive가 아니다

현재 stop 시에도 DTR/RTS를 내린다.

```text
m_serial->clear(QSerialPort::AllDirections)
m_serial->setRequestToSend(false)
m_serial->setDataTerminalReady(false)
m_serial->close()
```

### 문제점

영향 타이밍이 connect 시점만이 아니다.

```text
- connect
- disconnect
- core restart
- app exit
- resource error로 stop
- debug gateway 전환
- transport mode 전환
```

이 모든 시점에서 제어선 edge가 발생할 수 있다.

차량 무영향 기준에서는 open/close가 CSM/CAN 상태를 바꾸면 안 된다.

---

## 4. 코드상 확정 취약점 3: Host TX path가 살아 있다

현재 core process는 IPC로 `host_frame` 요청을 받을 수 있고, `SerialDrainRuntime::sendHostFrame()`은 같은 serial/TCP device로 write한다.

현재 구조는 다음이다.

```text
UI/Core IPC
  → hostFrameRequested
  → SerialDrainRuntime::sendHostFrame()
  → activeDevice()->write()
  → USB CDC OUT or TCP gateway
  → CSM firmware
```

### 문제점

Passive monitor에서는 TX path가 “사용하지 않음”이 아니라 **불가능**해야 한다.

현재는 구조적으로 다음이 가능하다.

```text
- host frame 송신
- control command 송신
- lease/heartbeat 송신
- CAN TX request 송신
```

차량 무영향 기준에서는 UI 버튼 숨김이나 사용자 실수 방지 수준으로는 부족하다. Passive mode에서는 IPC message 자체가 거부되어야 하고, core 내부 객체도 생성되지 않아야 한다.

---

## 5. 코드상 확정 취약점 4: ControlCycleRuntime은 실제 차량 방향 명령을 만든다

`ControlCycleRuntime`은 다음을 생성한다.

```text
- HOST_HEARTBEAT
- HOST_CONTROL_SESSION RENEW_LEASE
- HOST_CAN_TX_REQUEST
- control burst frames
```

### 문제점

이 기능은 벤치/제어시험에서는 필요할 수 있다. 그러나 차량 무영향 passive monitor에서는 존재 자체가 위험한 plane이다.

현재 구조상 control plane이 core target에 포함되어 있고, IPC 요청을 통해 접근 가능하다.

### 위험 흐름

```text
UI/control/debug mistake
  → control_cycle IPC
  → ControlCycleRuntime
  → Host TX queue
  → USB CDC OUT
  → CSM CAN TX
  → vehicle CAN 영향
```

최종형에서는 control plane이 passive build/runtime에서 분리되어야 한다.

---

## 6. 코드상 확정 취약점 5: Debug Gateway는 passive attach가 아니라 route replacement다

현재 debug gateway는 물리 COM을 직접 열고, 그 데이터를 TCP로 VSM core에 전달한다.

```text
COM port
  → scripts/vsm_debug_gateway.py
  → localhost TCP
  → VSM capture core
```

gateway script는 pyserial로 serial port를 열고, TCP로 받은 데이터는 다시 serial에 write할 수도 있다.

```text
TCP client data
  → self.serial.write(data)
```

### 문제점

Debug tap이 “관찰자”가 아니라 **물리 입력 경로를 대신 소유하는 gateway**가 된다.

따라서 debug on/off는 다음을 만들 수 있다.

```text
- COM close/open edge
- DTR/RTS/CDC 상태 변화
- CSM reset 가능성
- TCP backpressure와 serial read/write 정책 변화
- host TX path 우회 가능성
```

최종형 debug는 COM을 대신 여는 구조가 아니라, 이미 안전하게 수집된 core/capture/view에 attach하는 구조여야 한다.

---

## 7. 코드상 확정 취약점 6: VSM drain/backpressure가 CSM uplink를 막을 수 있다

현재 VSM core의 drain 구조는 개선되어 있다.

```text
QSerialPort/QTcpSocket readyRead
  → 64KB chunk read
  → DrainByteQueue
  → pipeline pump max 1MB
  → CaptureCoreRuntime ingest
```

Queue overrun은 fatal/capture invalid로 연결되는 개선도 있다.

하지만 이것은 **손실을 기록하는 보호**이지, 차량 영향 방지 보호는 아니다.

### 위험 흐름

```text
UI/core/capture/IPC/debug 부하
  → PC drain 또는 pipeline 지연
  → USB CDC IN을 늦게 비움
  → CSM serial TX backpressure
  → CSM uplink descriptor/pool 반환 지연
  → CAN_RX_SEGMENT enqueue fail
  → CAN task timing 영향 가능
  → ADCU system CAN period error 가능
```

실제 차량 로그에서도 VSM 저장 무결성은 정상이었지만, 보드 측 `canSegmentEnqueueFailTotal`, `uplinkPoolAllocFailTotal`, `serialBackpressureTotal` 계열 이상이 확인되었다. 이는 CSM의 uplink/segment 경계가 실제 병목 후보라는 뜻이다.

---

## 8. CSM firmware 구조상 필수 점검 영역

현재 VSM repository만으로 CSM firmware 전체는 확인할 수 없지만, 차량 무영향 기준에서는 아래 영역이 반드시 분리되어야 한다.

```text
- CAN RX ISR/task
- CAN RX ring buffer
- typed segment encoder
- uplink descriptor pool
- USB CDC TX task
- debug/event generator
- control command RX/TX task
- transceiver mode control
```

### 가장 위험한 구조

```text
CAN RX task
  → uplink segment allocation
  → USB TX availability 확인
  → descriptor/pool 부족 시 retry/block
```

이 구조라면 PC/VSM/Android/debug가 느려질 때 차량 CAN 수신 timing에 역압이 걸릴 수 있다.

### 최종형에서 금지해야 하는 것

```text
- CAN RX task에서 malloc
- CAN RX task에서 USB 상태 확인
- CAN RX task에서 blocking write
- CAN RX task에서 debug JSON/string 생성
- CAN RX task와 uplink/debug/control pool 공유
- uplink queue full이 CAN RX 처리 시간을 증가시키는 구조
```

---

## 9. 하드웨어/전기 경계 문제

소프트웨어가 아무리 좋아도 아래가 안전하지 않으면 USB 연결만으로 차량 CAN이 흔들릴 수 있다.

```text
- USB GND와 차량 GND 연결
- 노트북 충전기 접지
- 12V/48V ground bounce
- CAN transceiver common-mode
- USB shield 연결
- MCU reset 중 GPIO Hi-Z
- TXD floating
- STB/Silent floating
- reset 중 transceiver normal mode
```

### 최종형 기준

```text
- CAN side와 PC/USB side galvanic isolation
- TXD pull-up
- STB/Silent default-safe resistor
- MCU reset 중 transceiver silent/standby 보장
- USB 연결/해제 중 CANH/CANL dominant glitch 없음
```

---

## 10. PCAN/Kvaser와의 핵심 차이

PCAN/Kvaser가 차량에 문제를 덜 일으키는 이유는 앱 UI가 가벼워서가 아니다. 하드웨어/드라이버/API에서 차량 영향 가능성을 잘라내기 때문이다.

### PCAN / PEAK-System

PCAN-Basic의 `ListenOnly`는 CAN controller가 active event에 참여하지 않고 passive monitor로 동작하여 CAN network를 disturbance 없이 검사하기 위한 기능으로 문서화되어 있다.

### Kvaser

Kvaser silent mode는 CAN bus에 아무것도 transmit하지 않으며 ACK slot도 보내지 않고 dominant state를 만들지 않는다고 설명한다. Kvaser USBcan Pro 4xCAN Silent는 hardware로 silent가 고정되어 앱이나 제3자가 CAN bus에 영향을 줄 수 없도록 설계된 제품으로 설명된다.

### SocketCAN

SocketCAN도 CAN interface를 network interface로 취급하고 `listen-only` 같은 device-level 모드로 설정하는 구조를 제공한다.

### 결론

산업형 CAN tool의 핵심은 다음이다.

```text
Application이 아니라 channel mode가 안전을 보장한다.
Software UI가 아니라 CAN controller/transceiver boundary가 안전을 보장한다.
```

현재 VSM/CSM은 아직 이 수준이 아니다. 최종형은 이 철학을 따라야 한다.

---

# Part 2. 최종 완성 방향

## 1. 최종 목표: Vehicle-Impact-Free Contract

VSM/CSM 최종형은 아래 계약을 만족해야 한다.

```text
어떤 client가 언제 붙어도 vehicle CAN에는 영향이 없어야 한다.

- PC VSM 연결/해제 영향 없음
- Android VSM 연결/해제 영향 없음
- UI tab 전환 영향 없음
- debug tap on/off 영향 없음
- core restart 영향 없음
- capture start/stop 영향 없음
- IPC backpressure 영향 없음
- app crash 영향 없음
- USB cable plug/unplug 영향 없음
```

이를 위해 최종형은 `Passive Monitor`, `Active Test`, `Gateway/Control`을 분리해야 한다.

---

## 2. 모드 분리 원칙

## 2.1 Passive Monitor Mode

기본값이어야 한다.

```text
목적:
- 차량에 절대 영향 없는 관찰/기록/분석

허용:
- CAN RX 관찰
- capture.stream/index 저장
- live_latest 표시
- decoded tail 표시
- timing/value/alarm 분석
- graph materialization
- debug sidecar attach as observer

금지:
- CAN TX
- ACK participation, 가능하면 listen-only/silent
- error frame generation
- host TX
- control lease
- heartbeat
- USB OUT command
- DTR/RTS edge
- debug route replacement
```

## 2.2 Active Test Mode

벤치/정지 차량/통제된 시험에서만 허용한다.

```text
필수 조건:
- 물리 key/jumper
- software unlock
- UI danger state
- session lease
- audit log
- timeout auto-off
```

## 2.3 Gateway/Control Mode

차량 제어/주입 기능은 passive monitor와 빌드 또는 펌웨어 레벨에서 분리한다.

```text
권장:
- 별도 firmware image
- 별도 hardware jumper
- 별도 UI profile
- 별도 executable or feature flag
```

---

## 3. 최종 하드웨어 구조

가장 좋은 방향은 **hardware-silent passive tap**이다.

```text
Vehicle CAN
  ↓
Hardware-silent / listen-only CAN tap
  - isolated CAN transceiver
  - no TX capability in passive variant
  - no ACK if true silent required
  - reset-safe transceiver pins
  ↓
CSM passive capture firmware
  ↓ one-way telemetry
VSM/Android/Core/UI/Debug
```

## 3.1 권장 Variant

### Variant A: 최상위 안전형

```text
Hardware-silent CAN tap
- CAN TXD physically disconnected or gated off
- Transceiver permanently silent/listen-only
- ACK 없음
- dominant state 생성 불가능
```

PCAN/Kvaser hardware silent 계열에 가장 가까운 구조다.

### Variant B: 소프트웨어 listen-only형

```text
CAN controller listen-only mode
- firmware가 passive mode를 설정
- TX path disabled
- reset 중 transceiver safe
```

Variant A보다 낮은 등급이다. firmware bug나 reset state에 취약할 수 있다.

### Variant C: normal CAN interface + passive software option

```text
현재 구조에 가까움
- TX 가능 hardware
- software로만 막음
```

차량 무영향 최종 기준으로는 부족하다.

---

## 4. CSM firmware 최종 구조

## 4.1 CAN RX는 host/uplink와 완전히 분리

```text
CAN RX ISR/task
  → Fixed-size CAN RX ring
  → Immediate return
```

CAN RX task는 다음을 하지 않는다.

```text
- USB write
- malloc/free
- JSON/string formatting
- descriptor pool allocation wait
- blocking queue push
- debug event emit
- host connected state check
```

## 4.2 Uplink는 low-priority consumer

```text
CAN RX ring
  → Uplink encoder task
  → typed stream queue
  → USB/WiFi/Bluetooth transport
```

USB가 막히면 uplink만 손실된다.

```text
USB backpressure
  → uplink_drop_count 증가
  → capture_invalid 또는 telemetry_loss 표시
  → CAN RX task 영향 없음
```

## 4.3 Pool 분리

필수 분리:

```text
- can_rx_ring_pool
- uplink_segment_pool
- debug_event_pool
- control_command_pool
- usb_tx_pool
```

금지:

```text
- uplinkPoolAllocFail이 CAN RX task를 지연시키는 구조
- debug event 폭주가 CAN RX ring을 침범하는 구조
- host TX queue가 CAN RX pool과 공유되는 구조
```

---

## 5. VSM PC/Core 최종 구조

## 5.1 Passive serial open contract

기본 passive mode는 다음이어야 한다.

```text
open mode: ReadOnly
DTR policy: no-touch
RTS policy: no-touch
host TX: disabled
control cycle: disabled
```

진단에 반드시 표시한다.

```text
serial_open_mode = read_only
serial_dtr_policy = untouched
serial_rts_policy = untouched
host_tx_enabled = false
control_enabled = false
vehicle_impact_capability = impossible
```

## 5.2 Host TX compile/runtime 차단

Passive build에서는 아래가 불가능해야 한다.

```text
- CoreIpcClientRuntime::sendHostFrame
- CoreIpcServerRuntime host_frame accept
- CaptureCoreProcessRuntime::handleHostFrameRequested
- SerialDrainRuntime::sendHostFrame
- HostTxRuntime 생성
- ControlCycleRuntime 생성
```

단순 UI 숨김이 아니라 IPC/server/core/device writer 레벨에서 거부해야 한다.

## 5.3 Active Test unlock

Active Test를 허용할 경우 최소 조건:

```text
- physical jumper detected
- software unlock phrase
- session timeout
- UI red banner
- audit log
- default auto-stop
- control lease expiry
```

---

## 6. Debug 최종 구조

현재 debug gateway는 COM을 대신 열기 때문에 최종형에는 맞지 않는다.

최종형 debug는 다음이어야 한다.

```text
Core/CSM passive capture path
  → Debug sidecar query/tail attach
```

금지:

```text
Debug gateway가 COM을 직접 open
Debug on/off가 CSM USB/CDC state 변경
Debug on/off가 차량 CAN route 변경
Debug sidecar가 serial write 가능
```

허용:

```text
- capture stream/index 복사
- live view query
- bounded decoded tail query
- telemetry snapshot
- post-mortem artifact 생성
```

---

## 7. Android 최종 구조

Android도 PC와 동일한 안전 계약을 따라야 한다.

```text
Android UI
  → Android Core Service
  → Passive CSM telemetry link
```

금지:

```text
- Android app connect가 CSM CAN mode 변경
- Android lifecycle pause/resume이 CSM reset 유발
- Bluetooth/WiFi backpressure가 CAN RX task 지연
- debug screen 진입이 telemetry volume 증가로 CAN task 영향
```

Android에서도 passive contract를 표시해야 한다.

```text
vehicle_impact_capability = impossible
host_tx_enabled = false
can_mode = silent/listen_only
uplink_loss_count = N
capture_truth_valid = true/false
```

---

## 8. UI 최종 기준

UI는 기능 제공자가 아니라 안전 상태 표시자여야 한다.

필수 표시:

```text
CAN mode: hardware_silent / listen_only / normal
Host TX: disabled / enabled
Control: disabled / armed
Serial open: read_only / read_write
DTR/RTS: untouched / asserted
Debug: observer / route_replacement
Isolation: verified / unknown
Capture truth: valid / invalid
Uplink loss: count
Vehicle impact capability: impossible / possible / active
```

Active 가능한 순간에는 UI가 명확히 표시해야 한다.

```text
이 연결은 차량 CAN에 영향을 줄 수 있습니다.
```

Passive mode에서는 반대로 다음이 성립해야 한다.

```text
이 연결은 CAN TX/ACK/dominant/error-frame을 생성할 수 없는 경로입니다.
```

단, 이 문구는 hardware/firmware 검증이 끝난 뒤에만 표시할 수 있다.

---

## 9. 검증 기준

## 9.1 물리 검증

```text
- USB plug/unplug 중 CANH/CANL dominant glitch 없음
- VSM connect/disconnect 중 CAN error frame 증가 없음
- DTR/RTS 변화 없음 또는 변화해도 CSM reset 없음
- MCU reset 중 transceiver silent 유지
- PC 충전기 연결/분리 중 CAN common-mode 안정
- USB isolator 사용 전후 비교
```

## 9.2 firmware 검증

```text
- USB TX blocked 상태에서도 CAN RX task latency 변화 없음
- VSM 미연결 상태와 연결 상태의 CAN RX timestamp jitter 비교
- uplink queue full 시 CAN RX ring append latency 동일
- debug event 폭주 시 CAN RX drop 없음
- host disconnected 상태에서 CAN RX 계속 정상
```

## 9.3 VSM/Core 검증

```text
- passive mode에서 host_frame IPC 거부
- passive mode에서 control_cycle IPC 거부
- passive mode에서 SerialDrainRuntime write path 호출 0회
- passive mode에서 ReadOnly open 확인
- DTR/RTS no-touch 확인
- capture start/stop 중 transport line 변화 없음
- UI freeze 중 CSM uplink backpressure가 CAN task에 영향 없음
```

## 9.4 실차 검증

```text
- USB만 꽂기: ADCU period error 없음
- VSM 실행만: ADCU period error 없음
- VSM connect/disconnect 반복: ADCU period error 없음
- capture start/stop 반복: ADCU period error 없음
- debug sidecar attach/detach 반복: ADCU period error 없음
- Android connect/disconnect 반복: ADCU period error 없음
- 30분 이상 capture: system CAN period error 없음
```

---

## 10. 구현 우선순위

## Phase 1. VSM passive hardening

```text
1. PassiveMonitorOptions 도입
2. SerialDrainRuntime 기본 ReadOnly
3. DTR/RTS no-touch 기본값
4. Host TX passive mode hard reject
5. ControlCycle passive mode hard reject
6. diagnostics에 passive contract 표시
```

## Phase 2. CSM firmware isolation

```text
1. CAN RX ring 독립
2. uplink/debug/control pool 분리
3. USB backpressure가 CAN RX task에 역류하지 않게 설계
4. canSegmentEnqueueFail을 CAN RX timing과 분리
5. firmware telemetry에 task latency/high-water 추가
```

## Phase 3. Hardware passive safety

```text
1. listen-only/silent CAN controller mode 적용
2. hardware-silent variant 검토
3. isolated CAN transceiver 적용
4. reset-safe TXD/STB/Silent 회로 적용
5. USB/GND isolation 검토
```

## Phase 4. Debug architecture 재설계

```text
1. COM-owning debug gateway 폐기 또는 active-test 전용으로 강등
2. Debug sidecar를 core/capture view attach로 변경
3. debug on/off가 transport route를 바꾸지 않게 설계
```

## Phase 5. Acceptance gate

```text
모든 passive mode 실차 테스트에서:
- ADCU period error 0
- CAN error frame 증가 0
- CSM reset 0
- uplink loss가 CAN RX timing에 영향 0
- host TX write 0
- DTR/RTS transition 0 또는 영향 0 검증
```

---

## 11. 최종 Definition of Done

VSM/CSM 최종형은 다음이 모두 만족될 때 완료로 본다.

```text
1. Passive mode에서는 차량 CAN에 물리적으로 TX/dominant/error-frame 생성 불가능
2. Passive mode에서는 host TX/control IPC가 서버 단계에서 거부됨
3. USB connect/disconnect가 CSM reset 또는 transceiver mode 변화를 만들지 않음
4. PC/Android/UI/debug/core 부하가 CAN RX task timing에 영향 없음
5. Debug sidecar는 transport route를 바꾸지 않고 observer로만 동작
6. capture.stream/index는 authoritative truth이고, decoded tail/live latest는 display view로만 표시
7. UI가 vehicle impact capability를 명확히 표시함
8. 실차 반복 시험에서 ADCU period error 0
```

---

## 12. 최종 요약

현재 문제점은 “VSM이 느리다” 하나가 아니다.

더 정확한 문제는 다음이다.

```text
현재 VSM/CSM 일부 경계가 아직 vehicle-safe passive CAN interface가 아니라
USB CDC active endpoint + firmware uplink + optional host TX 구조라는 점이다.
```

최종 방향은 다음이다.

```text
VSM/CSM을 PCAN/Kvaser류 산업형 CAN interface처럼 재정의한다.

- 차량에 붙는 계층은 hardware/firmware로 passive-safe
- host/UI/debug/core는 그 위에서 query/view/capture만 수행
- active control은 별도 모드/별도 권한/별도 물리 조건에서만 허용
```

이보다 상위의 아이디어는 단순히 software read-only를 넣는 것이 아니라, **hardware-silent passive tap + one-way telemetry + host TX plane separation**을 제품 구조의 기본값으로 만드는 것이다.
