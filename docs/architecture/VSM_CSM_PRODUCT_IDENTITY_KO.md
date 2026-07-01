# VSM/CSM Product Identity And Execution Contract

## One Sentence

VSM/CSM은 차량 CAN에 영향을 주지 않는 2-bus passive evidence workstation이다.

## Product Identity

- 제품은 CAN 송신 도구가 아니라 passive monitor/logger/replay/decode workstation이다.
- 제품 기본값은 `vsm-ui.exe + vsm-capture-core.exe` 2프로세스다.
- 디버그는 기본 OFF인 `vsm-debug-tap.exe` 세 번째 프로세스다.
- CSM은 2-bus RX-only passive CAN front-end와 CDC evidence uplink를 분리한다.
- `capture.stream/index`만 authoritative truth다.

## Operator Promise

사용자는 실차에 CSM을 연결한 상태에서 USB를 꽂거나 빼고, VSM을 실행하거나 종료하고,
Debug Tap을 켜거나 꺼도 차량 CAN이 흔들리지 않는 제품을 기대한다. 소프트웨어는 이
약속을 코드만으로 증명할 수 없으므로, firmware evidence와 hardware evidence를
분리해 표시한다.

## Operating Modes

| Mode | Purpose | Vehicle use | COM owner | CAN TX/ACK | Proof |
| --- | --- | --- | --- | --- | --- |
| Passive Product | 실차 기록/분석 | allowed after hardware acceptance | Core only | disabled / no ACK | capture + health + external artifacts |
| Debug Tap | 제품 진단 | allowed when non-owning | none | none | debug artifacts only |
| Full Instrumented | bench/HIL 제어 | vehicle passive acceptance forbidden | lab tool/Core | allowed by explicit profile | bench evidence |
| Bench ACK Test | Kvaser/PCAN 송신 상대 | lab only | explicit lab profile | ACK/TX allowed | analyzer/Kvaser evidence |

## Evidence Taxonomy

- Authoritative capture truth: typed frame bytes in `capture.stream/index`.
- CAN frame truth: decoded CAN frames derived from accepted typed evidence.
- Display/materialized view: live latest, decoded tail, graph buckets, transport rows.
- Hardware passive proof: analyzer/scope/DTC artifacts, not CSM capability alone.
- Debug evidence: tap artifacts; never a substitute for capture truth.

## Product Completion Definition

The product is complete only when:

- Passive default cannot open serial read/write or run host TX/control/gateway.
- UI never owns raw typed stream or full live `TypedRecordList`.
- Core view notifications are bounded/coalesced and slow UI cannot block capture.
- Debug Tap can run as a third process without COM ownership or Core mutation.
- CSM passive firmware keeps both CAN front-ends passive across USB lifecycle.
- Vehicle-impact-free PASS is blocked until external hardware evidence matches
  capability claim references.

