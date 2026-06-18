# J4/PCAN Fault Triage Runbook

목표는 PCAN이 J4 쪽에서 초반만 들어오다 멈추는 현상을 다시 만났을 때, 원인을 추측하지 않고 증거로 가르는 것이다.

## Known Prior Failure

`full_analysis_truth_30s_20260616_180659`에서 확인된 사실:

- PCAN marker `0x4A`는 `bus1/J4`로 약 `0.625s` 동안만 들어온 뒤 중단됐다.
- Kvaser marker `0x6B`는 `bus0/MCP`로 30초 동안 계속 들어왔다.
- PCAN sender API는 당시 write error를 보고하지 않았다.
- CSM typed stream, `CAPABILITY`, `BOARD_HEALTH`, VSM capture finalize는 계속 살아 있었다.
- 따라서 이 사건은 VSM freeze나 CSM USB 전체 사망이 아니라 `PCAN <-> J4/bus1` 수신 경로 중단으로 본다.

## Do Not Do First

동일 현상이 재현되면 즉시 다음을 하지 않는다.

- 보드 리셋
- USB 케이블 재삽입
- VSM 종료
- PCAN/Kvaser 앱 종료

먼저 상태를 캡처해야 어느 쪽이 죽었는지 판정할 수 있다.

## Required Evidence

동일 시험은 `scripts/hil_analysis_truth_stress.py` 또는 `scripts/vsm_verify.py`의 full analysis truth scenario로 실행한다.

새 결과물에서 반드시 확인할 항목:

- `load_state.pcan_status_counts`
- `load_state.pcan_first_non_ok_status`
- `load_state.pcan_last_status`
- `load_state.kvaser_status_counts`
- `capture_report.source_timeline`
- `capture_report.health_delta`
- `final_status.typed_can_summary`
- `final_status.transport_summary`

## Decision Table

### A. PCAN 문제 확정

조건:

- `pcan_status_counts` 또는 `pcan_first_non_ok_status`에 `BUSOFF`, `BUSHEAVY`, `BUSLIGHT`, `QXMTFULL`, `INITIALIZE`, `ILLCLIENT`, `HWINUSE` 등이 남는다.
- `capture_report.source_timeline["0x4A"]`가 끊긴 뒤에도 CSM `BOARD_HEALTH`와 다른 source는 계속 정상이다.
- J4 외부 물리 조건을 건드리지 않아도 PCAN channel reset/USB reset 이후에만 복구된다.

판정: PCAN device/driver/channel 또는 PCAN-J4 물리 조합 문제.

### B. CSM J4 RX path 문제 확정

조건:

- PCAN status는 끝까지 `OK`.
- PCAN sender write error도 0.
- 같은 시간부터 `source_timeline["0x4A"]`만 끊긴다.
- `BOARD_HEALTH`는 fresh이고 bus0/MCP 또는 다른 evidence는 계속 증가한다.
- Kvaser를 J4에 물렸을 때도 같은 방식으로 bus1 수신이 중단된다.

판정: CSM J4/built-in CAN RX path 문제. 이 경우 CSM firmware에 J4 RX overrun/bus-off/error-passive 진단을 추가해야 한다.

### C. J4 물리층 문제 확정

조건:

- PCAN status가 bus heavy/off 또는 transmit queue full로 악화된다.
- Kvaser를 같은 J4 CAN-H/L에 병렬로 물렸을 때도 같은 시점에 에러/수신 중단이 관찰된다.
- CSM은 alive지만 J4 source가 끊기고, MCP/bus0는 계속 정상이다.

판정: J4 transceiver/배선/종단/접지/PCAN-J4 물리층 문제.

### D. VSM 문제 확정

조건:

- PCAN/Kvaser status는 정상.
- CSM health/capture_seq에는 bus1 수신 증가가 있다.
- 하지만 VSM capture/ledger/source timeline에만 `0x4A`가 없다.

판정: VSM parser/storage/ledger 문제. 단, 현재 known failure는 이 패턴이 아니다.

## Immediate Failure Procedure

동일 현상이 보이면 다음 순서로만 움직인다.

1. 시험을 10초 더 유지한다.
2. VSM snapshot/export가 가능하면 즉시 snapshot을 남긴다.
3. 시험 종료 후 artifact의 `result.json`, `capture_report.json`, `app_state.jsonl`, `process_metrics.csv`를 보존한다.
4. 그 다음에만 PCAN app/channel reset 또는 보드 reset을 시도한다.
5. reset으로만 복구되면 reset 전 상태를 원인 판정의 주 증거로 삼는다.

## Current Gap

현재 production CSM health는 J4 내부 CAN controller의 bus-off/error-passive/RX-overrun을 충분히 분리해서 올리지 않는다.  
따라서 `PCAN status OK + J4 source 중단`이 다시 나오면, 다음 firmware slice에서 J4 backend health/event를 추가해야 한다.
