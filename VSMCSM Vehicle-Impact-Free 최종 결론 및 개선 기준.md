# VSM/CSM Vehicle-Impact-Free 최종 결론 및 개선 기준

## Summary
현재 문제의 본질은 VSM UI/Core 성능 문제가 아니라 **차량 CAN에 영향을 줄 수 있는 active boundary가 제품 구조 안에 남아 있는 것**이다. 최신 로그상 VSM drain/capture는 오버런 없이 버티는 구간이 있지만, USB 연결/해제, GW on/off, CSM reset, CAN normal 참여, host TX/downlink가 차량 CAN을 흔들 수 있다.

최종 목표는 “잘 동작하는 logger”가 아니라 **PCAN/Kvaser silent/listen-only 계열처럼 차량에 붙는 순간부터 passive-safe인 산업 장비 구조**다.

## VSM 최종 개선 결론
- 기본 실행 모드를 `Passive Monitor`로 고정한다.
  - Serial open은 기본 `ReadOnly`.
  - DTR/RTS는 기본 no-touch.
  - host TX, control cycle, heartbeat, lease renew, TCP-to-serial write는 passive mode에서 IPC 서버 단계부터 hard reject.
  - UI 버튼 비활성화가 아니라 Core boundary에서 불가능해야 한다.

- Debug Gateway는 production passive 경로에서 제거한다.
  - 현재 GW는 COM을 직접 open/close하고 TCP→serial write가 가능하므로 observer가 아니다.
  - passive 실차 모드의 debug는 Core/capture/view sidecar attach만 허용한다.
  - COM-owning gateway는 lab active diagnostic 전용으로 격리한다.

- VSM UI/Core는 “차량 영향 가능성”을 명시해야 한다.
  - 표시 항목: `serial_open_mode`, `dtr_policy`, `rts_policy`, `host_tx_enabled`, `control_enabled`, `debug_mode`, `vehicle_impact_capability`.
  - `vehicle_impact_capability=possible/active` 상태에서는 실차 passive PASS를 주장하지 않는다.

- 기존 2+1 구조는 유지하되 안전 계약을 추가한다.
  - `vsm-capture-core.exe`는 COM 단독 owner.
  - `vsm-ui.exe`는 view query client.
  - debug/tap은 기본 OFF.
  - 단, 이 구조만으로 차량 무영향은 보장되지 않으므로 serial/host TX/passive contract가 별도 P0다.

## CSM 최종 개선 결론
- 실차용 CSM firmware는 별도 passive env로 분리한다.
  - MCP/builtin CAN은 listen-only 또는 hardware silent 기본.
  - host CAN TX, heartbeat/control session, command RX write path는 컴파일에서 제외.
  - USB CDC disconnect로 MCU reset을 걸지 않거나, reset 중 CAN transceiver가 silent/standby로 고정되어야 한다.

- CAN RX task와 uplink/debug/control은 절대 결합하지 않는다.
  - CAN RX ISR/task는 fixed ring append 후 즉시 return.
  - USB backpressure는 uplink loss/capture invalid로만 기록하고 CAN RX timing에 영향 주면 안 된다.
  - pool은 `can_rx`, `uplink`, `debug`, `control`, `usb_tx`로 분리한다.

- hardware passive safety가 최종 기준이다.
  - isolated CAN transceiver.
  - reset 중 TXD/STB/Silent safe resistor.
  - USB/GND isolation 검증.
  - passive variant는 물리적으로 TX/dominant/error-frame 생성 불가가 최상위 목표다.

## Acceptance Tests
- 실차 전 필수 bench gate:
  - USB plug/unplug 중 CANH/CANL dominant glitch 0.
  - VSM connect/disconnect 반복 중 CAN error frame 증가 0.
  - GW/debug on/off 중 CSM reset 0, CAN mode 변화 0.
  - passive mode에서 VSM/Core/GW serial write 호출 0.
  - passive firmware에서 host TX symbol/path 컴파일 제외 확인.
  - USB blocked/backpressure 상태에서 CAN RX task latency 변화 없음.

- 실차 gate:
  - USB만 꽂고 빼기 반복: 차량 ECU error 0.
  - VSM connect/disconnect 반복: 차량 ECU error 0.
  - capture start/stop 반복: 차량 ECU error 0.
  - debug sidecar attach/detach 반복: 차량 ECU error 0.
  - 30분 이상 dual bus capture: parser/storage loss 0 또는 명시 invalid, 차량 system CAN period error 0.

## Final Architecture Decision
- VSM 쪽 먼저 할 일:
  - passive mode contract 도입.
  - serial `ReadOnly/no-touch DTR/RTS` 정책화.
  - passive mode host TX/control/GW hard reject.
  - UI/diagnostics에 vehicle impact state 노출.
  - debug gateway를 실차 passive 경로에서 제거하고 sidecar-only로 전환.

- CSM 쪽 별도 할 일:
  - passive firmware env 분리.
  - CAN listen-only/hardware silent 기본화.
  - USB disconnect reset 제거 또는 transceiver safe 보장.
  - host TX/downlink 제거.
  - CAN RX task와 USB uplink 완전 분리.
  - hardware isolation/reset-safe 회로 검증.

## 더 나은 최종 아이디어
최선은 소프트웨어 read-only가 아니다. **CSM을 두 제품군으로 나누는 것**이다.

- `CSM-Passive`: 실차/현장 기본품. 물리적으로 TX 불가, silent/listen-only, one-way telemetry, debug도 sidecar만.
- `CSM-Active`: bench/control 전용품. 물리 jumper, 별도 firmware, 별도 UI profile, unlock/audit/timeout 필수.

이렇게 나누지 않으면 매번 UI, Core, GW, firmware flag로 active 기능을 막아야 해서 산업 안전 기준에서 약하다. 최종 기준은 “실수해도 차량에 영향 줄 수 없는 passive hardware/firmware”다.
