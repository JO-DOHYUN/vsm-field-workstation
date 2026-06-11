# CONTROL_EVIDENCE_CONTRACT_KO

이 문서는 VMS와 CSM 사이의 제어 evidence 의미를 고정한다.
Binary frame details are in [[TYPED_STREAM_PROTOCOL_V1_KO]].

## Evidence Chain

```text
Operator intent
  -> VMS requested command
  -> serial write queued/written
  -> CSM CONTROL_ACK accepted/rejected
  -> CSM CAN write attempt
  -> CSM CAN_TX_RAW actual sent audit
  -> external feedback CAN_RX_RAW
```

## Meaning Of Each Event

- Qt command queued: VMS가 operator intent를 typed downlink frame으로 만들고 transport queue에 넣었다.
- Qt serial write ok: OS serial buffer에 bytes를 넘겼다. CAN 성공이 아니다.
- `CONTROL_ACK REJECTED`: board가 명령을 거부했다. CAN success가 아니다.
- `CONTROL_ACK ACCEPTED`: board가 명령을 수락했다. CAN success가 아니다.
- `CAN_TX_RAW`: board hardware CAN write가 성공했다. 이것만 actual sent evidence다.
- feedback `CAN_RX_RAW`: 외부 장치 또는 VCU 반응 후보이며 sent audit과 별도 evidence다.

## State Rules

- `SerialOpen`: COM port open succeeded.
- `TypedDetected`: valid typed frame was parsed.
- `BoardAlive`: valid `CAPABILITY` and fresh `BOARD_HEALTH` were received with compatible protocol/profile.
- `HealthCurrent`: recent `BOARD_HEALTH` is within the allowed stale window.
- `ControlCapable`: capability says control path and TX audit are supported, safety state permits control, and fault flags are clear.
- `ControlPolicyAllowed`: active model pack `control_policy` permits the resolved target bus role and clamps requested rpm/steering to the declared profile limits.
- `ControlArmed`: VMS has sent heartbeat plus `HOST_CONTROL_SESSION action=arm`, and board accepted it.
- `HealthStale`, `HostTimeout`, `FaultLockout`, `Estop`: control success display is blocked.

## UI Rules

- VMS must not display Qt serial write as control success.
- VMS must not display `CONTROL_ACK` as final CAN success.
- VMS may display lab control while bring-up is active, but it must be visibly evidence-gated.
- ControlPage must show the active/default model policy profile, target role, allowed roles, and clamp limits next to the evidence checklist.
- Success text requires matching `CAN_TX_RAW`.
- Feedback status is shown separately from sent audit.

## Timeout And Fallback

- Missing `CONTROL_ACK` after request timeout: requested but not accepted.
- Missing `CAN_TX_RAW` after accepted timeout: accepted but not sent.
- Missing feedback after sent audit: sent but feedback not observed.
- Host heartbeat loss, lease expiry, estop, fault, or board health stale forces neutral/output-off policy according to CSM safety state.
