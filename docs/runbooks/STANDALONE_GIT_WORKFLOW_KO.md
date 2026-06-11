# STANDALONE_GIT_WORKFLOW_KO

## 기준

- 이 폴더는 VSM 단독 git repository root다.
- GitHub remote 기준: `https://github.com/JO-DOHYUN/vsm-field-workstation.git`.
- CSM firmware workspace/repository와 VSM repository는 분리한다.
- CSM firmware build가 필요할 때만 별도 CSM workspace에서 PlatformIO gate를 실행한다.

## 포함할 것

- source: `src/`, `qml/`, `tests/`, `shared/`, `data/`, `scripts/`, `packaging/`
- project harness: `AGENTS.md`, `START_HERE_KO.md`, `BRIEF.md`, `INDEX.md`, `.agents/`, `.codex/`
- docs/history: `docs/`, `history/`
- CI: `.github/workflows/`
- runtime directory markers: `replay_data/**/README.md`, `artifacts/README.md`

## 제외할 것

- build output: `out/`, `build/`, `.vs/`
- local run logs/screenshots: `.logs/`
- field/runtime captures: `replay_data/logs/*`, `replay_data/snapshots/*`
- generated validation output: `artifacts/*` except `artifacts/README.md`
- reference replay binary cache: `.ref-replay/`
- local CMake/user overrides: `CMakeUserPresets.json`, `*.user`

## 새 클론 시작 절차

```powershell
git clone https://github.com/JO-DOHYUN/vsm-field-workstation.git
cd vsm-field-workstation
```

그 다음 읽기 순서는 `AGENTS.md -> START_HERE_KO.md -> BRIEF.md -> INDEX.md`다.

## 커밋/푸시 절차

```powershell
git status --short --branch
git diff --check
git add -A
git commit -m "..."
git push origin main
```

하네스나 빌드 정책을 바꾸는 커밋은 `history/decisions/`에 이유, 기대 효과, rollback 조건을 같이 남긴다.

## 금지

- 이 VSM repository를 `HAMT2-platform/qt`로 다시 동기화하는 흐름을 기본값으로 되돌리지 않는다.
- 생성 산출물 또는 실차 capture를 source로 커밋하지 않는다.
- CSM firmware 변경을 VSM repository에 섞지 않는다.
