# VSM High-Load User-Route HIL

이 runbook은 direct COM reader가 아니라 실제 VSM 앱 경로를 검증한다.

## 목적

- VSM exe 실행, COM 연결, VSM 로그 시작/중지, 최종 `capture.stream` 생성까지 한 번에 확인한다.
- PCAN/Kvaser 송출 sequence 비교는 VSM이 만든 최종 capture만 대상으로 한다.
- projection sampling/drop은 UI 진단으로만 판단하고, capture truth와 섞지 않는다.

## 준비

- VSM Release exe: `out/build/x64-Release/can_monitor_qml_reboot.exe`
- CSM: `COM7` typed evidence stream
- Python: `py -3` 사용. `python`은 WindowsApps stub일 수 있으므로 쓰지 않는다.
- API load gate에는 PCANBasic.dll, Kvaser canlib32.dll, 각 장치 bus-on 상태가 필요하다.

## 실행

외부 송출 앱만 켜고 VSM route 30초:

```powershell
py -3 scripts\hil_vsm_user_route_stress.py --no-api-load --duration 30 --port COM7
```

PCAN/Kvaser API `1500fps + 1500fps`, 64 IDs, 30초:

```powershell
py -3 scripts\hil_vsm_user_route_stress.py --duration 30 --port COM7 --pcan-rate 1500 --kvaser-rate 1500 --id-count 64
```

60초와 5분은 `--duration 60`, `--duration 300`으로 올린다.

## 산출물

기본 위치:

- `artifacts/vsm_user_route_hil/<run>/result.json`
- `artifacts/vsm_user_route_hil/<run>/summary.md`
- `artifacts/vsm_user_route_hil/<run>/process_metrics.csv`
- `artifacts/vsm_user_route_hil/<run>/app_state.json`
- `artifacts/vsm_user_route_hil/<run>/app_state.jsonl`
- `artifacts/vsm_user_route_hil/<run>/capture_report.json`
- `artifacts/vsm_user_route_hil/<run>/sent_sequences.json`
- `artifacts/vsm_user_route_hil/<run>/session.meta.json`

VSM capture는 `replay_data/logs/<run>.typed/` 아래에 생겨야 한다.

## PASS 기준

- 새 typed capture directory가 생성됨.
- `capture.stream`, `capture.index`, `session.meta.json`이 있고 `.part`가 남지 않음.
- VSM process가 살아 있고 stop logging이 수락됨.
- typed CRC/length/seq/resync fault가 0.
- CSM `can_drop`, FIFO overflow delta가 0.
- API load 사용 시 PCAN/Kvaser sent sequence와 VSM final capture unique sequence가 정확히 일치.
- process private memory가 bounded: end-start 600MB 이하, 후반 slope 2MB/min 이하.

## FAIL 기준

- direct COM reader 결과만 있고 VSM capture가 없으면 FAIL.
- 이전 capture를 고르면 FAIL.
- VSM UI/control channel이 stop logging에 응답하지 않으면 FAIL.
- projection drop/sampling이 capture drop처럼 표시되면 FAIL.
