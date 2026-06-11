# HW_SW_DATA_CONTRACT_KO

이 문서는 board와 Qt 사이의 데이터 계약 요약이다. 최종 방향은 [[../BOARD_QT_FINAL_ARCHITECTURE]]를 따른다.

## Legacy Compatibility
legacy path는 20-byte packet, CRC8, DLC, `t_us` wrap semantics를 유지한다.
이 path는 기존 BIN/replay/import 호환을 위해 남기며 production live truth의 최종 형태로 확장하지 않는다.
VMS live production path는 typed evidence stream 전용이다.

## Typed Evidence Direction
production truth는 board가 낸 typed stream의 원본 byte다.
Qt는 voltage sample이나 board health를 fake CAN frame으로 변환하지 않는다.

## Evidence Types
- CAN RX raw: board가 수신한 CAN frame evidence
- CAN TX raw/audit: host 요청과 board hardware write 확인 evidence
- ADC/voltage raw: 전압 측정 evidence
- BOARD_HEALTH: capability, overflow, error counter, stream health evidence
- BOARD_EVENT: lease, arm, estop, state transition evidence
- CONTROL_ACK: host command lifecycle evidence

## Control Gate
host가 TX를 요청해도 board가 matching `CAN_TX_RAW`를 낼 때까지 실제 CAN 송신 성공으로 보지 않는다.
Qt write, `CONTROL_ACK`, `CAN_TX_RAW`, feedback은 서로 다른 evidence로 분리한다.
현재 lab control UI는 bring-up 용도로 허용하되, production success 표시는 capability/health/safety/audit chain 전까지 열지 않는다.

## Current Model Baseline
활성 기준 모델은 `data/vms_model_turn77_system_drive_merged_realcan_refresh2_final.json`이다.
값 해석 문제는 앱 구조 문제와 분리해서 다룬다.
