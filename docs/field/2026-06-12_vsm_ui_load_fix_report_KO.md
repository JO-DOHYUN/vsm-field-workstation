# 2026-06-12 VSM UI/Logging Load Fix Report

## 결론
- VSM live/logging 경로의 앱 멈춤 원인은 CSM 제어가 아니라 GUI thread로 너무 자주 들어오던 live projection/status 갱신이었다.
- typed parser와 `capture.stream` 원본 저장은 fault 없이 계속 진행됐지만, GUI event loop가 밀리면 HIL status/stop/finalize 명령 응답이 늦어졌다.
- 이번 패치 후 VSM user-route 30초/60초 검증에서 `app_state` timeout은 0건이고, typed capture는 `.part` 없이 정상 finalize됐다.
- 아직 전체 HIL PASS는 아니다. 남은 실패는 CSM `fifo` 증가 및 송신 대비 1프레임 수준의 capture mismatch로, VSM UI freeze와 분리해서 봐야 한다.

## 수정 요약
- `SerialWorker` live raw projection을 더 강하게 coalesce/sample:
  - UI projection flush interval `250ms`
  - max projected frames per flush `4`
  - pending projection keys hard cap `64`
- `AppController` live row queue를 bounded/low-rate로 조정:
  - live projection backlog soft/hard `128/256`
  - GUI flush chunk `16`, view chunk `4`
  - 0ms catch-up timer 제거
  - live panel inactive/paused 상태에서는 raw row batch를 UI queue에 쌓지 않음
- `LiveProjectionRuntime`/`TypedIngressRuntime` diagnostic status emit 빈도 제한:
  - projection status `500ms`
  - typed parser status `250ms`
- 원본 typed record 저장, parser, capture stream, truth counters는 변경하지 않았다.

## 재현 및 검증 근거
- Before:
  - `artifacts/vsm_user_route_hil/vsm_user_route_20260612_140650`
  - `app_state` poll timeout 반복
  - `capture.stream.part`, `capture.index.part`, `session.meta.json.part` 잔존
  - VSM stop/finalize 미완료
- After 30s:
  - `artifacts/vsm_user_route_hil/vsm_user_route_20260612_143231`
  - `app_state` timeout 0건
  - `capture.stream`, `capture.index`, `session.meta.json` 정상 생성
  - parser `crc=0`, `length=0`, `seq_gaps=0`, `resync_drop=0`
  - PCAN `3000/3000`, Kvaser `2999/3000`
- After 60s:
  - `artifacts/vsm_user_route_hil/vsm_user_route_20260612_143341`
  - `app_state` timeout 0건
  - `capture.stream`, `capture.index`, `session.meta.json` 정상 생성
  - parser `crc=0`, `length=0`, `seq_gaps=0`, `resync_drop=0`
  - PCAN `5999/6000`, Kvaser `6000/6000`
  - private memory는 초반 로딩 후 약 `124MB` 수준으로 안정

## 남은 분리 이슈
- 두 검증 모두 CSM `fifo` delta가 증가했다.
  - 30s: `fifo +3092`
  - 60s: `fifo +5990`
- 이 값은 VSM parser fault나 app freeze가 아니라 보드/입력 측 backlog 진단이다.
- 다음 단계는 CSM firmware를 바로 수정하기 전에, 동일 조건에서 debug gateway와 direct CSM counters를 함께 켜서 `MCP2515/J4 입력 -> CSM FIFO -> USB typed stream -> VSM capture` 중 어디서 빠지는지 분리해야 한다.

