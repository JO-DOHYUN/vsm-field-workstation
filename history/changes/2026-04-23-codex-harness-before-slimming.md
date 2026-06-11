# CODEX_HARNESS_KO.md

이 문서는 이 저장소를 **Codex 데스크탑 앱 + Git / GitHub review + skills + AGENTS.md + Local Environments + MCP** 구조로 안정적으로 굴리기 위한 운영 문서다.
`AGENTS.md`는 짧은 운영 맵으로 유지하고, 이 문서는 더 깊은 운용 지침을 담는다.

## 1. 왜 이렇게 분리하는가
최신 Codex 구조에서는 다음 분리가 효율적이다.
- `AGENTS.md`: 항상 적용되는 상시 규칙
- `BRIEF.md`: 현재 턴 목표 / 기준본 / acceptance
- `SKILL.md`: 특정 workflow가 필요할 때만 로드되는 상세 절차
- `docs/*.md`: 참고 문서와 깊은 설명
- `.codex/config.toml`: 프로젝트 설정

Codex는 `AGENTS.md`를 작업 전에 읽고, skills는 필요할 때만 로드한다. 또 Local Environments는 프로젝트별 setup script와 action을 Codex 앱 settings에서 구성해 공유할 수 있다. OpenAI도 짧은 `AGENTS.md`를 맵처럼 두고 더 깊은 정보는 별도 문서로 분리하는 구조를 권장한다. citeturn0search20turn0search4turn0search1turn0search11

## 2. 이 저장소의 권장 구조
```text
프로젝트루트/
├─ AGENTS.md
├─ BRIEF.md
├─ .codex/
│  └─ config.toml
├─ .agents/
│  └─ skills/
│     └─ main-code-progress/
│        └─ SKILL.md
├─ docs/
│  └─ CODEX_HARNESS_KO.md
├─ src/
├─ qml/
├─ data/
└─ scripts/
```

이 구조는 Codex 앱의 skills 지원과 프로젝트 설정 흐름에 맞다. Codex 앱은 데스크탑에서 병렬 thread, worktree, Git 기능을 제공하고, skills는 앱/CLI/IDE에서 공통으로 지원된다. citeturn0search13turn0search7

## 3. Codex 앱에서 시작하는 법
1. **프로젝트 루트 폴더**를 Codex 앱에서 연다.
2. 새 스레드를 시작할 때 아래 중 하나를 고른다.
   - **Local**: 현재 폴더를 직접 수정
   - **Worktree**: Git 저장소라면 분리된 작업트리에서 수정
   - **Cloud**: 원격 환경에서 실행
3. 첫 프롬프트는 아래처럼 주는 것이 좋다.

예시 1:
- `AGENTS.md`, `BRIEF.md`, available skills를 먼저 확인하고 현재 기준본과 이번 턴 acceptance를 정리해.`

예시 2:
- `main-code-progress 관점으로 현재 프로젝트의 build-risk와 invariants를 먼저 정리한 뒤 수정 계획만 제시해.`

Codex 앱은 데스크탑에서 병렬 thread, worktree, automations, Git 기능을 제공하며, Local Environments도 함께 쓸 수 있다. GitHub review는 PR 댓글 `@codex review`로 연결할 수 있다. citeturn0search13turn0search1turn0search10

## 4. Codex 앱 네이티브 작업 흐름
이 저장소에서 기본 흐름은 아래다.
1. 프로젝트 폴더를 연다.
2. `AGENTS.md`와 `BRIEF.md`를 먼저 읽게 한다.
3. 적합한 skill을 선택하거나 자연어로 유도한다.
4. Local 또는 Worktree에서 직접 수정한다.
5. Configure / Build / Run / Deploy는 Local Environments action으로 실행한다.
6. 검증 후 요약과 미검증 리스크를 남긴다.
7. Git 저장소라면 commit / branch / PR 흐름으로 넘긴다.

즉, **Codex 앱의 기본 결과물은 직접 수정된 프로젝트 상태와 Git 변경분**이다. ZIP은 기본 작업 단위가 아니라, Git 없이 전달해야 하거나 외부 handoff가 필요할 때만 쓰는 보조 형식이다. 이 점은 Codex 앱이 Git, worktree, local environments를 중심으로 설계되어 있다는 공식 문서 흐름과 맞다. citeturn0search13turn0search1turn0search24

## 5. Local Environments 권장 구성
Local Environments는 프로젝트별 setup script와 action 버튼을 둔다. 설정은 Codex 앱 settings에서 만들고, 생성된 파일을 저장소에 체크인해 공유할 수 있다. citeturn0search1

이 저장소에서는 아래 action 구성이 실용적이다.
- Configure Release: `cmake --preset vs-release-qt6`
- Build Release: `cmake --build --preset build-release`
- Configure Debug: `cmake --preset vs-debug-qt6`
- Build Debug: `cmake --build --preset build-debug`
- Deploy Release: `scripts\deploy_release.bat out\build\x64-Release`

## 6. GitHub 연계 최신 운용
이 저장소가 GitHub 저장소로 관리된다면 Codex GitHub 리뷰를 붙일 수 있다.

### 기본 흐름
- Codex Cloud를 설정한다.
- Codex settings에서 해당 저장소의 **Code review**를 켠다.
- PR 댓글에 `@codex review`를 쓰면 Codex가 리뷰를 단다.
- 자동 리뷰가 필요하면 **Automatic reviews**를 켠다.

이 흐름은 OpenAI의 GitHub integration 문서에 명시되어 있다. citeturn0search10

### 이 저장소에서 GitHub 리뷰에 넣어야 할 관점
`AGENTS.md`의 `Review guidelines`를 유지하고, 아래를 우선 본다.
- replay/live semantics 회귀
- graph truth drift
- fixed-axis / peak / zoom semantics 손상
- CMake / QML 등록 누락
- Windows 실행/배포 문서 모호성

### PR 댓글 템플릿 예시
- `@codex review for replay/live semantic regressions`
- `@codex review for graph truth drift and Qt registration issues`

## 7. MCP를 어디에 쓰는가
MCP는 외부 시스템을 붙일 때 쓴다. 이 저장소에서 유용한 축은 다음과 같다.
- 문서/지식 서버: 최신 Qt 문서, 사내 설계 문서
- 브라우저/Playwright: UI 확인, 배포 후 smoke check
- GitHub/이슈 추적: PR / 이슈 / 리뷰 맥락
- Figma: UI 설계가 있는 경우

Codex는 MCP를 지원하며, 외부 최신성이 필요한 정보나 도구에 적합하다. 반대로 저장소 내부 규칙은 `AGENTS.md`와 skill에 두는 편이 더 효율적이다. citeturn0search24turn0search4

원칙:
- 저장소 내부 규칙은 `AGENTS.md`와 skill로 처리한다
- 외부 최신성/도구가 필요한 것만 MCP로 연결한다
- 모든 것을 MCP로 넣지 않는다

## 8. Plugins를 언제 쓰는가
- **skills**: 직접 작성하는 작업 절차 형식
- **plugins**: skills + 연결 정보 + 배포 단위를 묶은 설치형 패키지

즉, 이 저장소에는 지금처럼 **repo skill 직접 작성**이 맞다. 여러 저장소/팀에 재배포할 때 plugin으로 승격하면 된다. skills는 로컬에서 유용성을 먼저 검증하고, 안정화되면 CI나 더 넓은 자동화로 확장하는 흐름도 공식 블로그에서 권장된다. citeturn0search4turn0search3

## 9. 이 저장소에서 직접 만든 파일이 더 나은 이유
범용 skill은 일반 코드리뷰/배포/문서화에는 유용하지만, 이 프로젝트의 핵심 제약은 매우 특수하다.
- CAN raw bridge
- 20바이트 packet 철학
- live / replay 분리
- full-range overview vs recent-window graph 분리
- Windows + Qt + windeployqt 실사용 흐름

그래서 **이 저장소 특화 규칙은 직접 만든 AGENTS/skill이 더 적합**하다. 남이 만든 것은 참고 자료로만 쓰고, 핵심 규칙은 로컬 파일이 소스 오브 트루스가 되어야 한다. OpenAI도 harness engineering 글에서 짧은 `AGENTS.md`를 맵으로 두고 구조화된 docs를 지식 베이스로 다루는 방식을 설명한다. citeturn0search2turn0search11

## 10. 반복 실수 교정 방법
같은 실수가 다시 나오면 아래 순서로 처리한다.
1. 이번 수정으로 문제를 고친다.
2. 왜 재발했는지 한 줄로 적는다.
3. 상시 규칙이면 `AGENTS.md`에 올린다.
4. 특정 workflow 문제면 `SKILL.md`에 올린다.
5. 설명/배경이 길면 이 문서나 별도 docs 문서로 뺀다.

이 흐름은 skills를 반복 작업의 패키지로 쓰고, AGENTS를 상시 가이드로 유지하는 최신 Codex 구조와 맞다. citeturn0search20turn0search4

## 11. 추천 시작 프롬프트
### 메인 진행 시작
`AGENTS.md와 BRIEF.md, available skills를 먼저 확인하고 현재 기준본·주목표·build-risk·invariants를 정리해. 수정은 바로 하지 말고 계획 먼저.`

### 직접 수정 시작
`main-code-progress를 기준으로 이번 턴 주목표만 크게 전진시켜. unrelated 축은 건드리지 말고, 수정 범위와 acceptance를 먼저 적은 뒤 수정해.`

### 리뷰 전용
`이번 변경을 replay/live semantics, graph truth, CMake/QML registration risk 중심으로 리뷰해.`

## 12. 유지 원칙
- `AGENTS.md`는 짧게 유지한다.
- `BRIEF.md`는 현재 턴 상태만 유지한다.
- 반복 workflow는 skill로 올린다.
- 설명이 길어지면 `docs/`로 뺀다.
- Codex가 같은 실수를 두 번 하면 파일 구조를 갱신해 재발을 줄인다.
