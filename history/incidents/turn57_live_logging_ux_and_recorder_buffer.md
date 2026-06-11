# ERROR_LOG_turn57_live_logging_ux_and_recorder_buffer

기준 소스
- work_turn56
- 사용자 요구: live 실시간성 저하 추가 개선, 로그 기록 여부가 명확히 보이도록 UX 재구성, 로그 시작은 즉시/저장은 중지 시 수동 파일명 지정

## 1. 원인 판단

### 1-1. 로그 기록 중 부하
기존 Recorder는 20바이트 프레임을 append할 때마다 `flush()`를 호출했다.
이 구조는 기록 활성 시 디스크 I/O 호출이 지나치게 잦아 live 실시간성 저하를 크게 만들 수 있다.

### 1-2. 로그 UX 혼선
기존 플로우는 `로그 시작 -> 폴더 선택 -> 즉시 최종파일 생성` 구조였다.
사용자 입장에서는
- 지금 실제 기록 중인지
- 어디에 쓰는 중인지
- 저장이 끝난 건지
- 취소/재저장 가능한지
가 불분명했다.

### 1-3. live projection 추가비용
live 분석과는 별개로 frame list projection이 백그라운드에서도 계속 돌고 있었다.
특히 live list 자체가 안 보이는 상태에서도 append/timeText 생성 비용이 들어갔다.

## 2. 이번 수정

### 2-1. Recorder 버퍼링
- per-frame flush 제거
- 내부 write buffer 사용
- 일정 바이트/프레임/시간 기준에서만 flush
- stop 시 강제 flush

### 2-2. 로그 저장 플로우 변경
- `startLog()`는 폴더 인자를 받지 않음
- 기본 로그 폴더 하위 temp 디렉터리에 즉시 임시 capture 시작
- `stopLog()`는 기록을 중지하고 `저장 대기` 상태로 전환
- `finalizePendingLogSave(path)`에서 사용자가 최종 파일명을 직접 지정
- `.bin` 외에 같은 basename의 `.meta.json`, `.model.json`도 같이 이동/저장
- `discardPendingLog()`로 임시 로그 폐기 가능

### 2-3. 로그 상태 가시화
- `logRecordingActive`
- `logPendingSave`
- `logStatusSummary`
- `suggestedLogSavePath`
- `logRecordedBytes`
- `logRecordedFrameCount`
추가

### 2-4. UI 반영
- Main.qml: 폴더 선택 제거, 저장 대화상자 추가, 상단 LOG 상태 배지 추가
- LivePage.qml: 기록 중 / 저장 대기 / 대기 상태 시각화, 저장하기/폐기 동선 분리
- SettingsPage.qml: 로그 상태 요약 표시

### 2-5. live projection trimming
- 숨겨진 live 패널에서는 live frame list append 생략
- recentFrames live append 제거

## 3. 기대 효과
- 로그 기록 중 디스크 flush 병목 완화
- 사용자가 현재 기록 상태를 즉시 인지 가능
- 최종 파일명/저장 위치를 중지 시점에 수동으로 지정 가능
- 저장 취소 후에도 저장 대기 상태 유지
- live list가 안 보일 때 불필요한 projection 비용 감소

## 4. 남은 확인 포인트
- Qt 실제 빌드/실행 미검증
- 기록 중 매우 고속 프레임 입력에서 still buffer flush 주기가 충분한지 확인 필요
- 저장 대화상자 취소 후 재시도 UX 확인 필요
- live가 여전히 무겁다면 다음 턴은 list/timeText 생성보다 `refreshTimingRows()/refreshValueRows()/refreshAlarmRows()` 주기와 setRows 비용을 더 직접 줄여야 함
