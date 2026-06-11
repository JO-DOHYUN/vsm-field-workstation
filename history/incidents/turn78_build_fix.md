# ERROR_LOG_turn78_build_fix

## Symptom
- turn77_full에 turn78 patch 적용 후 Release 빌드 실패
- VS 오류:
  - C2672 `std::min`: 일치하는 오버로드된 함수가 없습니다
  - C2737 `limit`: const 개체를 초기화해야 합니다
- 발생 위치:
  - `src/backend/SignalDecoder.cpp:365`

## Root Cause
- `maxTokens`는 `int`
- `tokens.size()`는 `qsizetype`
- `std::min(maxTokens, tokens.size())`에서 MSVC 템플릿 추론 실패
- 그래서 `limit` 초기화문 전체가 깨지며 C2737이 연쇄 발생

## Fix
- `tokens.size()`를 `int(...)`로 명시 변환
- 수정 후:
  - `const int limit = std::min(maxTokens, int(tokens.size()));`

## Scope
- 기능 로직 변화 없음
- turn78 patch의 슬라이더/값해석 변경 의도 유지
