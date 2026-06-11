---
name: main-code-progress
description: Qt/CMake 기반 CAN monitor 프로젝트의 메인 코드 진행 턴에서 사용한다. 기능 개발, 구조 개선, 성능 최적화, UX 마감처럼 주목표를 크게 전진시켜야 하지만 replay/live semantics, graph truth, build safety를 함께 지켜야 하는 작업에 적합하다. 순수 환경 복구나 순수 데이터 룰 정합 전용 턴에는 우선 사용하지 않는다.
---

# Main Code Progress

이 skill은 **메인 코드 진행 턴**에서 사용한다.
목표는 “많이 건드리기”가 아니라 **이번 턴 주목표를 크게 전진시키되 회귀와 build 실패를 줄이는 것**이다.

## 1. 시작 순서
항상 아래 순서로 시작한다.
1. `BRIEF.md`를 먼저 읽는다.
2. 현재 Codex 프로젝트 폴더와 branch / worktree 상태를 확인한다.
3. 기준본 파일명 또는 최신 handoff artifact가 있으면 적는다.
4. 주목표 1개, 부목표 최대 2개를 정한다.
5. 건드릴 파일 범위를 적는다.
6. invariant 3개 이상을 잠근다.
7. acceptance 3~5개를 적는다.

### 기본 원칙
큰 폭의 진척은 허용한다.
단, **이번 턴 주목표 축 안에서만** 크게 가져간다.
unrelated 축까지 동시에 확장하지 않는다.

## 2. build-risk 분기
아래를 건드리면 build-risk 턴이다.
- `AppController`
- 신규 C++ 파일
- `main.cpp`
- `CMakeLists.txt`
- QML 등록 / 모듈 / import
- signal / property / slot / `Q_PROPERTY`

build-risk 턴에서는:
- 목표를 더 줄인다
- 연결점 전체를 같은 수정 세트에 넣는다
- unrelated UI/UX 수정은 빼라
- 빌드 검증을 못 했으면 명시하라
- 선언/정의/CMake/QML 등록/타입 일치를 같이 확인하라

## 3. invariant 먼저 보호
수정 전에 invariant 3개 이상을 적는다.

예:
- replay controls visible and usable
- fixed-axis graph baseline stable
- micro zoom lock retained
- raw truth unchanged by display optimization
- full-range overview remains fixed while replay only moves cursor

이미 잘 되는 것을 깨뜨리면서 새 정리를 넣지 않는다.

## 4. 주목표별 실행 규칙

### 그래프 턴
우선순위:
1. raw truth 보존
2. display path 분리
3. fixed axis 유지
4. peak preservation
5. micro zoom / nested zoom lock semantics
6. renderer 비용 절감
7. cache / precompute / projection 비용 절감

금지:
- 그래프 턴에서 unrelated 축 확장
- truth를 바꾸는 display-only shortcut
- 한 번의 제스처 최적화 때문에 zoom history를 잃는 구조

### replay / seek 턴
먼저 확인:
- rebuild가 몰리는 지점
- analysis state vs UI projection 분리
- progress / time / frame 의미 일치
- seek 직후 상태 복원 비용

우선 방향:
- full rebuild 회피
- visible projection 비용 절감
- seek 후 상태 혼선 방지
- replay history counters와 live counters 의미 분리

### logging UX 턴
항상 명확히:
- 지금 녹화 중인지
- 저장 시점이 언제인지
- 파일명 / 경로 / 형식
- stop 후 저장 흐름
- 장시간 기록 중 UI 부하가 증가하는지

### overview / alarm 턴
절대 섞지 말 것:
- timing 문제
- value 문제
- bus / no-data / held 문제
- replay held/source 상태
- recovery 의미

### 모델팩 / 룰 / 값 해석 관련 턴
- 값 정합 문제와 앱 구조 문제를 분리한다
- 룰파일 수정은 UI/구조 리팩터링과 한 수정 세트에 섞지 않는다
- drive/system ID 충돌은 현재 기준 해석 우선순위를 명시한다

## 5. 수정 방식
- 먼저 읽고, 그 다음 최소 범위로 바꾸고, 마지막에 연결점을 닫는다
- 여러 파일이 연결된 구조라면 한 파일만 고치고 끝내지 않는다
- 실제로 확인하지 못한 동작은 성공이라고 쓰지 않는다
- "좋아 보이는 구조"보다 현재 기준본과 acceptance를 우선한다
- Codex 앱에서는 **직접 수정 + 변경 요약**을 기본 전달 방식으로 본다

## 6. Codex harness 친화 운영
이 저장소에서는 다음이 효율적이다.
- 상시 규칙은 `AGENTS.md`에 유지
- 현재 턴 목표와 기준본은 `BRIEF.md`에 유지
- 반복 workflow는 skill에 유지
- 더 깊은 Codex 운용 문서는 `docs/CODEX_HARNESS_KO.md`에 유지

반복해서 생기는 교정이 있으면:
1. 이번 수정으로 문제를 고치고
2. 재발 방지 규칙을 `AGENTS.md` 또는 이 skill에 반영하고
3. 필요하면 `docs/CODEX_HARNESS_KO.md`의 운용 규칙도 갱신한다

## 7. 검증 규칙
가능하면 아래를 확인한다.
- preset 기준 configure 가능 여부
- release build 가능 여부
- 주목표 acceptance 충족 여부
- 관련 탭 진입 / 조작 / 복귀 흐름

못 했으면:
- 못 했다고 적는다
- 무엇이 미검증인지 적는다
- 사용자가 바로 확인할 테스트 포인트 2~4개를 남긴다

## 8. 결과물 구성 규칙
기본 결과물은 아래 조합이다.
- 프로젝트 폴더의 직접 수정 결과
- 변경 파일 목록
- 핵심 변경점
- build-risk 여부
- 미검증 리스크
- 테스트 포인트

선택형 handoff가 필요한 경우만 아래를 추가한다.
- patch ZIP
- handoff 문서
- export bundle

신규 클래스 추가 시에는
- `.h`
- `.cpp`
- `CMakeLists.txt`
- 관련 `main.cpp` / QML 연결
을 함께 닫는다.

선택형 handoff만으로 안 끝나면 반드시 적는다.
- 삭제 파일
- 이름 변경
- 수동 이동
- 수동 설정
- build-risk
- 미검증 항목

## 9. GitHub / 리뷰 연계 시 행동
이 저장소가 GitHub와 연결되어 있으면, 직접 수정할 때도 PR 리뷰 기준을 염두에 둔다.
- 변경 이유가 `BRIEF.md`의 현재 목표와 연결되는지 본다
- 리뷰어가 바로 볼 수 있게 영향 범위를 명확히 남긴다
- replay/live 의미 회귀, graph truth drift, CMake/QML 등록 누락은 우선 검토한다
- 반복되는 리뷰 피드백은 `AGENTS.md`의 Review guidelines로 승격한다

## 10. 답변 및 산출물 규칙
항상 짧고 정직하게 정리한다.
- 기준본
- 이번 턴 주목표
- 핵심 변경점
- build-risk 여부
- 미검증 리스크
- 테스트 포인트
- 변경 파일 또는 handoff 파일

## 11. 한 줄 요약
이 skill의 핵심은 **주목표 안에서만 크게 전진하고, 현재 프로젝트/worktree 기준으로, 기준본/회귀/빌드 안전을 먼저 잠그는 것**이다.
