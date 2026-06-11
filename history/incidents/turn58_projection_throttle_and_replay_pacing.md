# ERROR_LOG_turn58_projection_throttle_and_replay_pacing

기준
- latest base: work_turn57.zip
- 참고: turn50 완성까지 총괄 구성검토, turn57 BRIEF

문제 판단
1. turn57 이후 live는 로그 기록 경로는 가벼워졌지만, timing/value/alarm row projection이 여전히 자주 돌아 전체 실시간성이 부족함
2. 특히 hidden/secondary panel까지 projection 성격의 갱신이 따라오면 live 체감이 무거워짐
3. replay seek/rebuild는 truth 철학은 유지하지만 checkpoint 저장 시 state 캐시까지 통째로 복사해 비용이 큼

이번 수정
## 1) projection throttle
- timing/value/alarm row projection에 각각 최소 갱신 간격 추가
- 현재 panel 활성 여부 + pending live backlog + log recording 상태를 반영해 interval 가변화
- visible page일 때는 빠르게, hidden/secondary 상태일 때는 느리게 projection

## 2) value preview/detail 비용 축소
- preview cache 생성 조건을 value panel 또는 selected id 위주로 축소
- value detail refresh는 Value 탭 실제 활성 상태에서만 수행
- hidden overview 쪽에서 detail decode가 계속 도는 비용 제거

## 3) replay rebuild pacing 가변화
- seek remaining 길이에 따라 replay rebuild chunk / minChunk / budget 동적 조정
- 짧은 seek는 빠르게, 긴 seek는 chunk를 키워 완료 시간을 단축

## 4) replay checkpoint slim copy
- checkpoint 저장 시 cachedTimingRow / cachedPreviewInfo / cachedValueAlarmInfo / cachedValueRow 제거
- dirty flag만 유지한 slim state로 checkpoint 저장
- stride도 완화하고 최대 checkpoint 수도 축소해 복사 부하 감소

의도
- 분석 truth는 유지
- 보이는 row/model projection만 덜 자주/덜 무겁게 수행
- replay는 snapshot truth를 유지하면서 seek 완료 시간을 더 줄임

로컬 검증 포인트
1. live 상태에서 turn57 대비 CPU/버벅임 개선 여부
2. hidden value/alarm 상태에서 live 반응성 개선 여부
3. replay seek 후 완료까지 체감 시간 감소 여부
4. 같은 BIN 반복 탐색에서 checkpoint 효과가 유지되는지
