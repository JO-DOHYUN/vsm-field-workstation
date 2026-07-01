# INDEX

VSM 문서 허브다. 현재 기준은 Passive-Safe 2+1 제품 구조이며, 과거
gateway/control/full-instrumented 문서는 history 또는 bench/lab 참고로만 본다.

## Current Product Baseline

- [[START_HERE_KO]]: 현재 저장소 진입점.
- [[BRIEF]]: 현재 기준, 보존 계약, 즉시 다음 작업.
- [[docs/architecture/VSM_CSM_PRODUCT_IDENTITY_KO]]: 최종 제품 목적성, 목표,
  identity, 모드 분리 기준.
- [[docs/PROJECT_FOLDER_GUIDE_KO]]: source/docs/history/runtime data 폴더 기준.

## Core Architecture

- [[docs/architecture/PROJECT_CONSTITUTION_KO]]: VSM/CSM 제품 헌장.
- [[docs/architecture/VSM_CORE_DATA_VIEW_TAP_ARCHITECTURE_KO]]: Core-owned Data
  Plane, View Query Plane, Optional Debug Tap Plane.
- [[docs/architecture/VSM_PASSIVE_SAFE_2PLUS1_ARCHITECTURE_KO]]: Passive Product
  profile, 2+1 process split, vehicle-impact-free runtime policy.
- [[docs/architecture/VSM_PASSIVE_DEBUG_TAP_PRODUCT_ARCHITECTURE_KO]]: non-owning
  debug tap process contract.
- [[docs/architecture/VSM_DATA_OWNERSHIP_BOUNDARY_RULES_KO]]: owner/consumer/drop
  policy와 forbidden boundary rules.
- [[docs/architecture/TYPED_STREAM_PROTOCOL_V1_KO]]: typed stream v1 human
  contract.
- [[shared/protocol/typed_stream_v1]]: CSM/VSM shared binary contract.
- [[docs/architecture/CONTROL_EVIDENCE_CONTRACT_KO]]: control evidence contract.

## Passive Product Evidence

- [[docs/reviews/passive_product_boundary_audit]]: truth taxonomy, scenario audit,
  capability claim/proof separation.
- [[docs/hardware/CSM_PASSIVE_FRONTEND_REQUIREMENTS]]: passive CAN front-end
  firmware/hardware requirements.
- [[docs/hardware/CSM_PASSIVE_FRONTEND_ACCEPTANCE]]: analyzer/scope/DTC acceptance
  requirements.
- [[docs/hardware/CSM_FIELD_SKU_BOM_RULES]]: field SKU BOM and hardware evidence
  reference rules.
- [[docs/hardware/CSM_USB_CAN_ISOLATION_POLICY]]: USB/CAN isolation and hotplug
  disturbance policy.

## Build And Verification

- [[docs/runbooks/BUILD_AND_VERIFY_KO]]: CMake configure/build/ctest/smoke.
- [[docs/runbooks/VSM_CAPTURE_CORE_MEMORY_VERIFY_KO]]: capture-core memory
  30s/10m/1h verification.
- [[docs/runbooks/VSM_HIGH_LOAD_USER_ROUTE_HIL_KO]]: user-route HIL.
- [[docs/runbooks/VSM_VERIFY_RUNNER_KO]]: HIL/debug/report launcher catalog.
- [[docs/runbooks/RELEASE_AND_DEPLOY_KO]]: portable release and installer hook.
- [[docs/ai_harness/BUILD_VERIFY_POLICY_KO]]: build/test/reporting policy.

## Harness

- [[AGENTS]]: 상위 운영 규칙.
- [[HARNESS_MASTER_KO]]: harness redesign and skill boundary rules.
- [[.agents/skills/harness-maint/SKILL.md]]: harness/document boundary workflow.
- [[.agents/skills/typed-evidence/SKILL.md]]: typed evidence/protocol workflow.
- [[.agents/skills/capture-core-memory/SKILL.md]]: capture-core hot path workflow.
- [[.agents/skills/qt-build-verify/SKILL.md]]: build verification workflow.

## History

- [[history/INDEX]]: old decisions, incidents, cleanup notes.
- 과거 COM-owning gateway, full-instrumented control, one-off HIL notes는 history
  기준이며 Passive Product acceptance 기준이 아니다.
