# VSM Debug/Verification 후속 검증 체크리스트

작성 기준: 2026-06-16

## 현재 빌드 상태

- Release build: PASS
- 명령: `cmake --build --preset build-release`
- 최종 확인: `ninja: no work to do.`
- 실행 파일: `out/build/x64-Release/can_monitor_qml_reboot.exe`

## 이번 패치로 확인할 핵심

- 앱 안의 `현재 앱 부하 30s`, `현재 앱 Truth 30s`는 새 VSM을 띄우면 안 된다.
- 기존에 열린 VSM/COM7/로그 경로를 그대로 사용하고, 외부 PCAN/Kvaser 송신기만 별도 프로세스로 실행해야 한다.
- 결과 문구는 `typed record`와 `CAN_RX frame`을 분리해서 표시해야 한다.
- `CAN_RX_SEGMENT` 사용 시 `log_typed_records`는 segment record 수이고, 실제 CAN frame 수는 `can_rx_frames` 또는 capture report의 `segment_frames`가 기준이다.

## 1. 앱 UI 경로 검증

조건:
- VSM 1개만 실행
- COM7 연결
- PCAN/Kvaser 연결 상태 유지

검증:
- 설정/도구의 디버그 검증 패널에서 `현재 앱 부하 30s` 실행
- 작업 관리자에서 새 `can_monitor_qml_reboot.exe`가 추가로 생기지 않는지 확인
- 완료 후 `artifacts/vsm_verify/attached_load_30s_*` 생성 확인

PASS 기준:
- 새 VSM 창 없음
- `attached_result.json` 생성
- `pass=true`
- `can_rx_frames > 0`
- `log_typed_records > 0`
- VSM 응답 유지

## 1-A. 현장 단일버스 MCP/PCAN 검증

조건:
- VSM 1개만 실행
- COM7 연결
- MCP2515 쪽에 PCAN만 연결
- Kvaser/J4는 없어도 됨

앱 시나리오:
- `현재 앱 MCP/PCAN 30s`
- `현재 앱 MCP Truth 30s`

공식 실행기 시나리오:
```powershell
py -3 scripts/vsm_verify.py run --scenario pcan_mcp_load_30s
py -3 scripts/vsm_verify.py run --scenario analysis_pcan_mcp_load_30s
```

PASS 기준:
- 새 VSM 창 없음
- `attached_result.json`의 `pass=true`
- `can_rx_frames`가 30초 1000fps 기준 약 30000
- capture report: `seq_gaps=0`
- capture report: `crc=0 length=0 dropped_bytes=0`
- capture report: `capture_seq_gaps=0 capture_seq_duplicates=0`
- capture report: `drop_total=0 fifo_delta=0 fifo_total=0`
- `typed_can_summary` 최종 `RX parity ... miss 0`
- `truth frames` 증가, `loss 0`, `overrun 0`
- private memory가 급격히 단조 폭증하지 않음

2026-06-16 현장 조건 확인 결과:
- `attached_pcan_mcp_load_30s_20260616_113806`: PASS, `CAN_RX 29999`, FIFO/drop/CRC/seq gap 0
- `attached_pcan_mcp_truth_30s_20260616_113936`: PASS, `CAN_RX 30000`, FIFO/drop/CRC/seq gap 0, graph series 1, alarm rows 6

## 2. 1000+1000fps user-route 검증

검증:
- `현재 앱 부하 30s`
- 필요 시 같은 경로로 5분 endurance 추가

PASS 기준:
- capture report: `seq_gaps=0`
- capture report: `crc=0 length=0 dropped_bytes=0`
- capture report: `capture_seq_gaps=0`
- capture report: `drop_total=0 fifo_delta=0`
- 30초 기준 송신 1000+1000fps이면 `segment_frames=60000` 근처
- VSM `Responding=False` 없음
- 로그 finalize 정상

참고 명령:
```powershell
py -3 scripts/analyze_typed_capture.py replay_data/logs/<session>.typed
```

## 3. Truth 분석 검증

검증:
- `현재 앱 Truth 30s`
- fixture 모델: `tests/fixtures/analysis_truth_stress_model.json`

PASS 기준:
- timing/value/alarm/graph가 raw UI projection이 아니라 AnalysisRuntime truth 기준으로 계산됨
- `truth loss 0`
- `analysis overrun 0`
- noise ID가 모델 경보를 오염시키지 않음
- graph min/max/peak 보존

## 4. UI/성능 검증

검증:
- Live Raw Tail, ID State, Diagnostics, Performance를 열어둔 상태로 부하 실행
- 창 크기 변경과 탭 전환을 같이 수행

PASS 기준:
- 앱 멈춤 없음
- raw tail 표시 지연은 진단에 표시
- 주기/값/경보/그래프 사실 계산은 누락 없음
- performance summary에 병목 모듈이 남음
- raw ledger / analysis / projection / capture storage 진단이 서로 섞이지 않음

## 5. 메모리 검증

검증:
- 30초 통과 후 5분 endurance에서 process metrics 기록

PASS 기준:
- private memory가 단조 폭증하지 않음
- 후반부 memory slope 안정
- raw ledger는 segment/temp store 기준으로 증가 원인이 설명 가능
- normal mode에서 debug gateway/profiler가 hot loop에 들어오지 않음

## 6. 판정 보류 항목

- CSM/PCAN/Kvaser 실제 송신 mismatch는 source별로 분리 판정한다.
- Kvaser 단독 불안정은 VSM FAIL로 보지 않는다.
- 단, VSM capture/ledger/replay/AnalysisRuntime truth가 깨지면 VSM FAIL이다.
