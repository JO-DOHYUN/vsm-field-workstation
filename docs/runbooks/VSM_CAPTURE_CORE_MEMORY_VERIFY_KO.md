# VSM_CAPTURE_CORE_MEMORY_VERIFY_KO

## 목적
VSM live capture-core memory refactor가 실제 장시간 high-load에서 효과가 있는지 검증한다.
30초 통과만으로 성공을 선언하지 않는다.

## 기본 원칙
- PASS는 actual VSM route 기준이다.
- direct COM reader PASS는 VSM PASS가 아니다.
- debug gateway raw capture는 crash/hang evidence일 뿐, VSM final capture validation 대체물이 아니다.
- UI projection drop은 허용될 수 있지만 capture truth silent loss는 허용되지 않는다.

## 필수 산출물
각 run은 아래 artifact를 남긴다.

- `result.json`
- `process_metrics.csv`
- `ui_responsiveness_report.json`
- `capture.diagnostics.json`
- transport/session snapshot JSON
- final `capture.stream` parse summary
- memory slope/plateau 판정

## Test Matrix
최소 행렬:

```text
A. storage OFF, graph inactive, 10m
B. storage ON, graph inactive, 10m
C. storage ON, graph active 4 series, 10m
D. storage ON, graph inactive, 1h
E. storage ON, graph active 4 series, 1h
```

가능하면 추가:

```text
F. live UI paused ON, storage ON, 10m
G. model/rules disabled, storage ON, 10m
H. debug gateway ON, storage ON, 10m
```

## PASS 기준
- process private bytes warmup 이후 plateau 형성.
- 1시간 기준 warmup 이후 growth <= 600MB.
- 1시간 기준 slope <= 2MB/min.
- UI event loop `stall_1000ms_count == 0`.
- raw ingress overrun 0.
- slab alloc fail 0.
- capture writer overrun 0.
- parser CRC/length fault 0 또는 원인 설명 가능.
- CSM ring clear / segment enqueue fail 0 또는 CSM 원인으로 분리.
- analysis overrun 0.
- capture finalize 성공.
- final capture parse 성공.

## FAIL 판정
아래 중 하나면 실패다.

- private memory가 시간 비례로 계속 증가.
- 입력 중단 후 UI 회복에 긴 backlog drain 시간이 필요.
- process는 살아있지만 QML event loop가 장시간 멈춤.
- hidden Qt queued batch 때문에 telemetry queue는 낮은데 process memory만 증가.
- capture writer queue가 cap에 닿음.
- analysis queue overrun 발생.
- raw ingress/slab overrun 발생.
- graph inactive인데 graph memory 증가.

## 원인 분리 기준
- storage OFF에서 안정, storage ON에서 증가: writer/capture path.
- UI paused에서 안정, UI active에서 증가: projection/QML model path.
- graph inactive에서 안정, graph active에서 증가: graph history/bucket/render path.
- model disabled에서 안정: analysis/decode/model path.
- raw ingress/slab 증가가 먼저: VSM drain/parser 소비 지연.
- CSM backpressure/segment enqueue fail이 먼저: CSM uplink/USB endpoint 경계.
- telemetry queue는 모두 안정인데 process memory만 증가: Qt queued event, implicit sharing retention, allocator fragmentation 의심.

## 보고 형식
보고는 아래만 단정한다.

```text
duration
input rate
storage on/off
graph active/inactive
private bytes start/warmup/end/max
memory slope
queue high-water
overrun/fatal counters
UI stall counters
capture finalization result
primary bottleneck boundary
```

HIL을 직접 돌리지 않았으면 PASS라고 쓰지 않는다.
