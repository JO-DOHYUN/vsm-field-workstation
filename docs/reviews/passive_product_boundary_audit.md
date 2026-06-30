# Passive Product Boundary Audit

This is the current execution contract for the VSM/CSM Passive Product slice.
It exists to prevent old gateway/control/debug assumptions from returning under
new file names.

## Truth Taxonomy

- Authoritative capture truth: `capture.stream` plus `capture.index`, containing
  accepted typed frame bytes and typed record order.
- CAN frame truth: decoded `CAN_RX_RAW` and expanded `CAN_RX_SEGMENT` entries
  derived from authoritative capture truth. This is analysis input, not UI
  projection.
- Display truth: bounded materialized views such as `live_latest`,
  `decoded_can_tail`, graph buckets, and transport rows. Display drop is not
  capture loss.
- Hardware passive proof: external analyzer/scope/DTC/bench artifacts. CSM
  capability fields are claims and references; they are not proof by themselves.
- Debug evidence: `vsm-debug-tap.exe` sidecar artifacts. Debug tap is not the
  authoritative capture truth and cannot be used to claim vehicle-impact-free
  PASS by itself.

## Scenario Audit

| Scenario | Current required behavior | Evidence counter/view | Failure mode if violated |
| --- | --- | --- | --- |
| Vehicle CAN connected before USB | CSM drains both CAN front-ends and discards payload while host absent; no typed payload staging | host absent discard counters, host absent gap total | stale CAN payload replayed into next VSM capture |
| USB plug | CSM enters uplink/session quarantine only; CAN front-end keeps passive drain running | USB session open, DTR change, attach quarantine total | MCP/transceiver reset or normal mode changes vehicle CAN |
| USB unplug | CSM discards pending uplink payload and continues host-absent drain | USB session close reported on next open | old segment is emitted after reconnect |
| VSM connect | Core owns COM read-only and asserts DTR only as declared session gate | runtime profile, transport policy | UI or debug process opens COM directly or write-capable |
| Log start/stop | Capture writer owns `capture.stream/index`; UI reads views only | capture progress, diagnostics | UI state becomes capture truth owner |
| Debug Tap on/off | `vsm-debug-tap.exe` subscribes to Core IPC only; it never opens COM or changes Core state | debug tap ready/trace/summary | diagnostics backpressure affects capture or vehicle |
| Core crash/reconnect | UI reconnects to Core and queries views; CSM remains passive | core health, process lifecycle | UI restart toggles board active behavior |
| `.part` capture | Incomplete session is reported as recovery evidence, not normal finalized log | recovery report, capture diagnostics | partial capture is misread as PASS |
| Hardware evidence missing | VSM shows Software Passive Prototype / hardware unverified | passive state gate | capability spoof is displayed as verified product PASS |
| Bus count mismatch | Product is still 2-bus; mismatch is a blocking diagnostic, not a 1-bus product path | CSM profile match | wrong firmware upload is mistaken for VSM receive bug |

## Adopted Review Corrections

1. The word `truth` must always name one of the taxonomy entries above.
2. Host-absent no-replay must be testable as separate units: no payload staging
   while absent, discard queued payload on close/open, epoch bump on open, summary
   before first new CAN segment.
3. Hardware evidence fields in `CAPABILITY` are claims/references only. VSM must
   independently validate external artifacts before `verified_passive`.
4. The product is fixed as 2-bus, but bus mismatch warnings must remain. Removing
   mismatch diagnostics hides wrong firmware or wiring.
5. USB attach quarantine is not CAN front-end quarantine. It is a CDC/uplink
   session cleanup state only.
6. Verified passive gate requires concrete external artifacts: analyzer PASS,
   scope/hotplug PASS, DTC delta zero, matching artifact id, and hotplug count.
7. Hardware redesign requirements must be executable at schematic/BOM/netlist
   review level, not only stated as architecture goals.

## Required Static Guard Outcomes

- Passive VSM must not open serial as read/write outside `RuntimeProfile`.
- Passive VSM must not run host TX, control cycle, or COM-owning gateway.
- UI must not consume raw typed stream, `TypedRecordList`, or storage
  `frameBytes` in production live paths.
- Passive CSM must compile out host downlink, host TX, control TX, test TX, USB
  reconnect reset, and MCP normal mode transitions.
- Passive CSM must advertise two RX buses or be rejected by VSM as incomplete.

