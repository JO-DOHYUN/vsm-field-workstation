# 2026-06-12 VSM Debug Gateway Overflow Diagnosis

## 결론
- 이번 검증에서 VSM UI/capture route가 PCAN 누락의 1차 원인이라는 증거는 나오지 않았다.
- CSM `fifo`는 VSM을 아예 띄우지 않은 gateway-only 상태에서도 증가했다.
- 1 ID / 100 fps PCAN+Kvaser 송신 검증에서 PCAN 누락 28개는 gateway raw stream과 VSM final `capture.stream` 양쪽에 동일하게 없었다.
- 따라서 해당 run의 PCAN 누락은 VSM TCP/UI/capture 경로 이후가 아니라, CSM이 USB typed stream으로 올리기 전 단계에서 이미 발생한 것으로 판단한다.
- 단, 현재 버스에는 하네스 `--no-api-load` 상태에서도 약 RX 1600 fps / TX 1600 fps 수준의 기존 트래픽이 들어오고 있었다. “무부하”가 아니었다.

## 수정한 디버그 모드
- `scripts/vsm_debug_gateway.py`에서 serial read/raw file write와 VSM TCP forwarding을 분리했다.
- raw capture는 TCP 전송 지연에 막히지 않는다.
- VSM이 TCP를 못 받으면 `tcp_queue_dropped_bytes`, `tcp_forward_errors`, `tcp_queue_max_chunks`로 별도 기록한다.
- 이번 검증에서 `tcp_queue_dropped_bytes=0`, `tcp_forward_errors=0`, `tcp_queue_max_chunks=1`이었다.

## Artifacts
- debug gateway no-api user route:
  - `artifacts/vsm_user_route_hil/vsm_user_route_20260612_135734`
  - VSM final capture: `replay_data/logs/vsm_user_route_20260612_135734.typed`
  - result: FAIL, reason `CSM can_drop/fifo increased`
  - gateway parser: crc 0, length 0, seq_gaps 0, resync_drop 0
  - VSM capture parser: crc 0, length 0, seq_gaps 0, resync_drop 0
  - CSM health delta: fifo +1061, can_drop +0

- debug gateway 1 ID / 100 fps PCAN+Kvaser user route:
  - `artifacts/vsm_user_route_hil/vsm_user_route_20260612_135828`
  - VSM final capture: `replay_data/logs/vsm_user_route_20260612_135828.typed`
  - result: FAIL, reason `CSM can_drop/fifo increased`, `pcan_compare mismatch`
  - sent: PCAN 1000, Kvaser 1000
  - VSM final capture: PCAN 972, Kvaser 1000
  - gateway raw stream: PCAN 972, Kvaser 1000
  - identical PCAN missing sequence count: 28
  - CSM health delta in VSM capture: fifo +1194, can_drop +0
  - CSM health delta in gateway raw: fifo +1486, can_drop +0

- gateway-only, no VSM process:
  - `artifacts/vsm_debug_gateway/standalone_20260612_140016`
  - gateway parser: crc 0, length 0, seq_gaps 0, resync_drop 0
  - CSM health delta: fifo +969, can_drop +0
  - This proves FIFO growth can occur without VSM/UI/capture running.

## 판단
- VSM 앱 route는 이번 runs에서 typed parser corruption 없이 최종 capture를 만들었다.
- VSM process memory stayed bounded in user-route runs.
- 현재 문제는 “VSM UI가 느려서 원본 사실값을 잃는다”로만 보면 안 된다.
- 적어도 이번 artifacts 기준으로는 CSM FIFO가 입력 CAN/board path에서 증가하고, 그 결과가 USB typed stream 이전에 반영된다.

## 다음 확인
- PCAN/Kvaser 외부 송신기를 모두 끈 상태에서 gateway-only 30s를 먼저 실행해 baseline FIFO 증가가 0인지 확인한다.
- baseline FIFO가 0이면 PCAN/Kvaser rate sweep을 gateway-only로 먼저 수행한다.
- gateway-only가 PASS인데 VSM user route만 FAIL이면 그때 VSM absorb path를 다시 원인으로 잡는다.
- gateway-only도 FAIL이면 CSM firmware/hardware CAN ingress/FIFO policy를 별도 축으로 봐야 한다.
