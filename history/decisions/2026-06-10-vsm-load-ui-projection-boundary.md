# 2026-06-10 VSM Load/UI Projection Boundary

## Decision

VSM high-load failure 대응은 CSM firmware가 아니라 VSM live projection boundary부터 수정한다.

## Reason

- Direct COM typed-stream integrity는 통과할 수 있지만 VSM 사용자 경로는 UI projection, diagnostics, logging이 같이 동작한다.
- 실패 증거는 process memory growth와 `Responding=False`로, storage truth보다는 GUI/controller backlog가 더 강한 원인이다.
- `TypedIngressRuntime`은 storage append 후 UI batch emit 순서를 이미 갖고 있으므로, raw capture path는 보존하고 display projection만 bounded/sampled로 분리할 수 있다.

## Applied Boundary

- `LiveProjectionRuntime`이 worker-side에서 CAN_RX display projection을 coalesce/sample한다.
- `CAPABILITY`, `BOARD_HEALTH`, `BOARD_EVENT`, `CONTROL_ACK`, `CAN_TX_RAW`는 critical evidence로 AppController에 유지 전달한다.
- AppController live pending queue는 hard cap을 가지며, drop은 projection-only counter로 분리한다.
- `TransportSession`은 capture storage, typed parser, board health, host TX, live projection diagnostics를 분리 표시한다.

## Rollback

- typed storage tests에서 `capture.stream` record count/CRC/sequence가 깨지면 즉시 rollback한다.
- control evidence에서 `CONTROL_ACK`와 `CAN_TX_RAW` 분리가 깨지면 rollback한다.
- VSM route HIL에서 projection drop이 final capture mismatch로 이어지면 projection boundary를 재설계한다.

## Non-Goals

- 이번 결정은 CSM firmware throughput 한계 변경이 아니다.
- direct COM reader PASS를 VSM product PASS로 인정하지 않는다.
