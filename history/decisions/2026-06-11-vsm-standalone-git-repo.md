# 2026-06-11 VSM Standalone Git Repository

## Decision

VSM Qt workstation을 기존 CSM 통합 repository의 `qt/` 하위 폴더 동기화 방식에서 분리해, `JO-DOHYUN/vsm-field-workstation` 단독 repository로 관리한다.

## Reason

- VSM과 CSM의 작업 주기, 빌드 도구, runtime artifact 크기가 다르다.
- 단독 Codex thread/account가 VSM 폴더만 보고도 작업을 시작해야 한다.
- `qt/` path prefix 동기화는 실수하면 root 경로가 깨지는 위험이 있다.

## Expected Gain

- VSM source, docs, harness, CI가 한 repository root에서 일관되게 동작한다.
- generated capture/build/log 출력은 `.gitignore`로 분리하고, 필요한 field evidence만 docs/history에 승격한다.
- CSM firmware build는 별도 gate로 유지해 VSM 작업과 섞이지 않는다.

## Rollback

VSM과 CSM을 다시 단일 repository로 묶어야 하는 명확한 릴리즈/조직 요구가 생기면, 이 repository를 read-only archive로 두고 CSM repository에 subtree/submodule 전략을 문서화한 뒤 이전한다. 단순 편의 때문에 `HAMT2-platform/qt` 수동 복사 workflow로 되돌리지 않는다.
