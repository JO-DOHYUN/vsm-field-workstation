# VSM/CSM Product Identity And Execution Contract

## One Sentence

VSM/CSM은 실차 2-bus CAN을 안정화 후 ACK-capable observe-only로 관찰하고,
host TX/control 없이 authoritative typed evidence를 저장/분석하는 제품이다.

## Product Identity

- 제품은 범용 USB-CAN 송수신 도구가 아니다.
- 제품은 2-bus monitor/logger/replay/decode/evidence workstation이다.
- CSM 제품 모드는 ACK-capable observe-only다. 안정된 host session 이후 ACK는
  허용되지만, host-originated CAN data TX/control/downlink/test TX는 금지된다.
- USB/DTR/session 안정 전 CSM은 CAN front-end initialization을 지연한다.
- VSM 기본 실행은 `vsm-ui.exe + vsm-capture-core.exe` 2프로세스다.
- Debug는 기본 OFF인 `vsm-debug-tap.exe` 세 번째 프로세스이며 COM/USB를
  소유하지 않는다.
- `capture.stream/index`만 authoritative truth다.

## Operator Promise

사용자는 실차에 CSM을 연결한 상태에서 USB를 꽂거나 빼고, VSM을 실행하거나
종료하고, Debug Tap을 켜거나 꺼도 차량 CAN을 흔들지 않는 제품을 기대한다.
소프트웨어는 이를 코드만으로 최종 증명할 수 없으므로 firmware evidence와
hardware/external evidence를 분리해 표시한다.

## Operating Modes

| Mode | Purpose | Vehicle use | COM owner | CAN behavior | Proof |
| --- | --- | --- | --- | --- | --- |
| Passive Product | 실차 기록/분석 | allowed after hardware acceptance | Core only | ACK-capable observe, no host TX/control | capture + health + external artifacts |
| Pre-session Safe | USB/DTR/session 안정 전 | required | none/Core not open | no ACK guarantee, no payload replay, no host TX/control | lifecycle counters |
| Debug Tap | 제품 진단 | allowed when non-owning | none | no CAN behavior change | debug artifacts only |
| Full Instrumented | bench/HIL 제어 | vehicle passive acceptance forbidden | lab tool/Core | explicit TX/control allowed by profile | bench evidence |
| Listen-only Diagnostic | no-ACK 물리 진단 | lab/diagnostic only | Core/lab | no ACK, no TX/control | analyzer evidence |

ACK capability is not host TX capability. A CAN controller in normal observe mode
may ACK valid frames, but Passive Product still must not expose host TX/control.

## Evidence Taxonomy

- Authoritative capture truth: typed frame bytes in `capture.stream/index`.
- CAN frame truth: decoded CAN frames derived from accepted typed evidence.
- Display/materialized view: live latest, decoded tail, graph buckets, transport rows.
- Hardware passive proof: analyzer/scope/DTC artifacts, not CSM capability alone.
- Debug evidence: tap artifacts; never a substitute for capture truth.

## Product Completion Definition

The product is complete only when:

- Passive default cannot run host TX/control/downlink/test TX/gateway.
- CSM enters pre-session safe state before any Serial wait or uplink setup.
- CSM switches from deferred CAN front-end hold to ACK-observe only after host
  session quarantine, quiet window, and CAN front-end initialization succeed.
- UI never owns raw typed stream or full live `TypedRecordList`.
- Core view notifications are bounded/coalesced and slow UI cannot block capture.
- Debug Tap can run as a third process without COM ownership or Core mutation.
- Vehicle-impact-free PASS is blocked until external hardware evidence matches
  capability claim references.

## Current Additive Contract: Deferred CAN Front-End Init

The current CSM Passive Product firmware must not initialize MCP/built-in CAN
front ends during USB power-up. It must hold CAN front-end initialization before
a stable CDC/DTR session, clear stale uplink/session payload at session open,
emit `CAN_FRONTEND_PRESESSION_HOLD`, wait the configured quiet window, initialize
MCP/built-in CAN front ends, arm ACK-observe only after initialization succeeds,
and emit `CAN_FRONTEND_SESSION_READY` before VSM treats CAN_RX_SEGMENT evidence
as trusted live capture input.

This firmware handoff still does not prove vehicle-impact-free hardware. Final
PASS still requires external analyzer/scope/DTC evidence.
