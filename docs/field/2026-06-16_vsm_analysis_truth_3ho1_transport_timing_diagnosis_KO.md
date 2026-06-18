# 2026-06-16 (3호1) 실차 로그 전송 오류/주기 지연 판정

## 대상
- 로그 폴더: `replay_data/logs/vsm_analysis_truth_20260615_221209(3호1).typed`
- 스크린샷: 로그 폴더 내 스크린샷 및 사용자 첨부 화면
- 분석 명령:
```powershell
py -3 scripts\analyze_typed_capture.py "C:\WORKS\VS\turn81_full_buildfix2\replay_data\logs\vsm_analysis_truth_20260615_221209(3호1).typed"
```

## 핵심 근거
- final capture:
  - `records=212673`
  - `seq_gaps=26`
  - `crc=0`, `length=0`, `dropped_bytes=0`
  - `capture_seq_gaps=805125`
  - `capture_seq_reorders=32568`
  - `segment_frames=604556`
- board health:
  - `can_rx_delta=1407158`
  - `drop_total=0`
  - `fifo_total=0`
  - `serial_delta=504476`
  - `max_queue=2`
- screenshot live 상태:
  - typed parser faults 1234
  - drop 1198, crc 10, seq 26
  - board health `can_drop 0`, `fifo_overflow 0`
  - raw ledger `620988`, truth `620988`

## 주기 지연처럼 보인 이유
- 여러 ID가 공통으로 약 `92000 ms` 수준의 max gap을 가진다.
- 동시에 p95/p99는 주요 10 ms/20 ms ID에서 정상 기대값에 가깝다.
- 예:
  - bus1 `0x204`: p95 `10.27 ms`, p99 `10.58 ms`, max `92071.84 ms`
  - bus0 `0x111`: p95 `20.05 ms`, p99 `20.14 ms`, max `92101.77 ms`
  - bus0 `0x117`: p95 `20.06 ms`, p99 `20.16 ms`, max `92081.65 ms`
- 모든 ID가 동시에 비슷한 대형 max gap을 갖는 형태는 차량 CAN 주기가 실제로 그만큼 길어진 것보다, typed/capture continuity가 깨진 구간을 주기 계산에 섞은 현상과 더 잘 맞는다.

## 판정
- CSM CAN controller drop/FIFO overflow 증거는 현재 로그 기준 0이다.
- VSM final capture에는 typed stream seq gap과 capture_seq gap이 있다.
- 따라서 (3호1)의 대형 주기 지연 표시는 실차 CAN 자체 지연으로 단정하면 안 된다.
- 현재 더 강한 원인은 host/typed stream/capture continuity gap이며, 기존 정책은 이 gap을 모델 주기 오류와 충분히 분리하지 못했다.

## 적용한 코드 정책
- `AnalysisRuntime`이 `captureSeq` 연속성을 추적한다.
- `captureSeq` gap을 건너는 ID별 interval은 `transportContaminated`로 표시한다.
- 오염 interval은 evidence에서 삭제하지 않고 `transportGapText`, `transportGapCount`로 남긴다.
- 오염 interval은 실제 CAN period ERR로 승격하지 않는다.
- 실제 CAN period 판정은 capture continuity가 유지된 interval만 사용한다.

## 남은 현장 검증
- 차량에서 다시 같은 조건으로 로그를 찍을 때, 주기 탭에서 `transportContaminated`/`capture_seq gap`이 발생하는지 확인한다.
- 발생하면 CAN 버스 주기 불량이 아니라 transport/capture 연속성 문제로 우선 분류한다.
- 발생하지 않는데도 특정 ID만 p95/p99/max가 규칙을 벗어나면 그때 실제 차량 CAN 주기 또는 모델 기대 주기 정책을 점검한다.
