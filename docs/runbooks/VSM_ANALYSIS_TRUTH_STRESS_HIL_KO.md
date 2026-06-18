# VSM Analysis Truth Stress HIL Runbook

## 목적
- 고부하에서도 주기, 값, 경보, 그래프가 UI projection/sample이 아니라 typed CAN truth 기준으로 계산되는지 검증한다.
- PASS는 실제 VSM user-route 또는 현재 실행 중인 VSM attached route에서 `송신 manifest -> VSM capture.stream -> raw ledger/AnalysisRuntime -> UI snapshot -> graph series`가 모두 일치할 때만 인정한다.
- 실차 연결 중에는 CAN 부하 송신 시나리오를 실행하지 않는다. PCAN/Kvaser 송신 부하가 차량 버스에 직접 들어갈 수 있다.

## 시나리오
- `analysis_truth_30s`: 별도 VSM exe를 새로 실행한다. 4개 fixture ID와 noise로 1000+1000fps 30초를 검증한다.
- `full_analysis_truth_30s`: 별도 VSM exe를 새로 실행한다. 160개 모델 ID와 noise로 timing/value/alarm/graph를 동시에 검증한다.
- `attached_pcan_mcp_truth_30s`: 이미 실행/연결된 VSM에 PCAN/MCP 단일 버스 fixture 부하를 붙인다.
- `attached_full_pcan_mcp_30s`: 이미 실행/연결된 VSM에 PCAN/MCP 단일 버스 full fixture 1000fps 30초를 붙인다.
- `attached_full_pcan_mcp_60s`: 이미 실행/연결된 VSM에 PCAN/MCP 단일 버스 full fixture 2000fps 60초를 붙인다.
- `attached_full_load_30s`: 이미 실행/연결된 VSM에 PCAN+Kvaser dual full fixture 1000+1000fps 30초를 붙인다.

## 모델팩
- Smoke fixture: `tests/fixtures/analysis_truth_stress_model.json`
  - `0x510`: timing/DLC histogram
  - `0x520`: range value alarm
  - `0x521`: reserved-bit alarm
  - `0x530`: graph latest/min/max/peak
- Full fixture: `tests/fixtures/full_load_truth_stress_model.json`
  - `0x510-0x51F`: timing stress
  - `0x520-0x55F`: range alarm stress
  - `0x560-0x57F`: reserved-bit alarm stress
  - `0x580-0x59F`: flag alarm stress
  - `0x5A0-0x5AF`: graph peak/min/max stress
  - noise ID는 모델 경보를 오염시키면 FAIL이다.

## 실행
독립 user-route:
```powershell
py -3 scripts\vsm_verify.py run --scenario full_analysis_truth_30s --port COM7
```

현재 앱 attached 단일 PCAN/MCP 송신기만 dry-run:
```powershell
py -3 scripts\vsm_verify.py run --scenario full_pcan_mcp_load_30s --dry-run
```

현재 앱 UI에서는 설정 탭의 `디버그/검증` 패널에서 attached 시나리오를 선택한다. 패널은 모델팩, fps, ID 수, 산출물 위치, 안전 주의사항을 표시한다.

## 산출물
- 공통 root: `artifacts/vsm_verify/<scenario>_<timestamp>/`
- `result.json`: runner envelope
- `stdout.log`, `stderr.log`: 외부 Python 실행기 로그
- `expected_sender_manifest.json`: 송신기 기준 모델 ID별 송신 수
- `attached_result.json`: 현재 앱 attached route 최종 판정
- `truth_validation_result.json`: final capture와 snapshot을 비교한 truth 판정
- `app_snapshot_before_load.json`, `app_snapshot_after_load.json`, `app_snapshot.json`: UI/analysis/graph/performance snapshot
- `latest_capture_report.txt`: 최신 capture 요약
- `process_metrics.csv`: 독립 HIL일 때 프로세스 메모리/CPU 추적

## PASS 조건
- typed CRC, length, resync, seq gap이 0이다.
- `capture_seq64` gap/duplicate가 0이다.
- CSM `can_drop`과 FIFO overflow 증가가 0이다.
- sender manifest의 모델 frame 수와 VSM final capture source marker count가 일치한다.
- AnalysisRuntime `truth_loss=0`, `analysis_overrun=0`이다.
- timing/value/alarm rows가 full fixture 모델 ID별 기대 상태와 일치한다.
- graph series latest/min/max/peak가 expected manifest와 허용오차 안에서 일치한다.
- UI raw row cap 또는 projection sampling은 truth PASS를 대체하지 않는다.
- 독립 HIL에서는 VSM process memory가 bounded이고 control timeout/Responding=False가 없어야 한다.

## 전송 오염 주기 판정
- `capture_seq gap`이나 typed stream seq gap이 있으면 해당 gap을 건넌 ID별 interval은 `transportContaminated`로 표시한다.
- 이 interval은 증거에서 삭제하지 않는다. 다만 모델 주기 ERR로 단정하지 않는다.
- 실제 CAN 주기 지연은 transport continuity가 유지된 frame interval에서만 판정한다.
- 현장 로그에서 `can_drop=0/fifo=0`인데 여러 ID가 같은 대형 max gap을 보이면 차량 CAN 지연보다 host/typed transport/capture continuity 문제 가능성이 우선이다.
