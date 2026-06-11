# FIELD_VALIDATION_KO

실차/벤치 확인은 자동 테스트 성공과 분리해서 evidence-first로 기록한다.

## 기준 산출물
- VSM 실행 파일: `out/build/x64-Release/can_monitor_qml_reboot.exe`
- Field RC portable: `out/build/x64-Release/portable_field_rc_<YYYYMMDD>`
- Release manifest: `portable_field_rc_<YYYYMMDD>/RELEASE_MANIFEST.json`
- 로그 루트: `replay_data/logs`
- 최신 typed capture 요약: `py scripts/field_latest_capture_report.py`
- 세부 capture 요약: `py scripts/analyze_typed_capture.py replay_data/logs/<session>.typed`

## 실차 전 Smoke
1. Release build, full `ctest`, exe startup smoke가 통과했는지 확인한다.
2. CSM `portenta_h7_m7_mid_mcp2515_j4_dual_csm` PlatformIO build가 통과했는지 확인한다.
3. 최신 `portable_field_rc_<YYYYMMDD>`가 생성됐고 `RELEASE_MANIFEST.json`에 exe/model/SBOM hash가 있는지 확인한다.
4. Portable exe를 실행해 main window가 5초 이상 유지되는지 확인한다.
5. Live 페이지 `TRANSPORT` 진단이 `OK` 또는 원인 설명이 있는 `WARN/ERR`인지 확인한다.
6. Control 페이지에서 model policy row, target role, clamp limit가 보이는지 확인한다.
7. Control 페이지는 `CONTROL_ACK`와 `CAN_TX_RAW`가 분리되어 보이는지 확인한다.
8. Replay 페이지에서 최신 capture를 열고 DLC, bus 분포, board health, board event row를 확인한다.
9. Graph 페이지에서 체크박스 선택/해제 후 목록 스크롤과 색상이 유지되는지 수동 확인한다.

## 실차 중 기록
- 연결 직후 `CAPABILITY`와 `BOARD_HEALTH`가 보이지 않으면 board alive로 보지 않는다.
- `TRANSPORT ERR`는 parser fault 또는 host TX queue drop을 먼저 본다.
- `Live delay WARN`은 frame age, stats age, pending live queue, sampled view drop을 같이 본다.
- 제어 성공은 matching `CAN_TX_RAW`만 기준으로 기록한다.
- bus0/bus1 판단은 `can_bus`, `capability_bus`, `board_events`, `board_health`를 함께 본다.
- bus0/bus1 hot-plug 또는 터미널 교체 시점은 시간과 배선 상태를 별도 메모한다.
- DLC 길이는 Replay typed diagnostics와 `analyze_typed_capture.py` 결과가 일치해야 한다.
- Graph slider/checkbox 불편이 재현되면 재현 시각, replay/live 여부, 선택 key, 스크롤 위치를 같이 남긴다.

## 실차 후 판정
- `field_latest_capture_report.py`로 최신 session을 먼저 요약한다.
- `seq_gaps`, `crc`, `length`, `dropped_bytes`가 있으면 capture 정상으로 단정하지 않는다.
- `health max_queue`, `fifo_delta`, `drop_total`, `events top`을 현장 원인 후보로 남긴다.
- replay UI의 operator verdict와 script report가 충돌하면 raw `capture.stream`을 우선한다.
- 결과는 `docs/field/2026-06-02_<short_result>_KO.md`에 남긴다.
- 결과 문서에는 capture path, manifest package id, bus0/bus1 판정, DLC 판정, control evidence 판정, 미검증 항목을 반드시 포함한다.
