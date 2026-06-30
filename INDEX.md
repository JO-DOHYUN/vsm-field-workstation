# INDEX

이 파일은 VSM 문서 허브다.
상위 운영 규칙은 [[AGENTS]], 새 진입은 [[START_HERE_KO]], 현재 기준본은 [[BRIEF]]를 따른다.

## Current Truth
- [[START_HERE_KO]]: 새 채팅/새 계정 진입 문서.
- [[BRIEF]]: 현재 기준본, 보존 기능, 즉시 다음 작업.
- [[docs/PROJECT_FOLDER_GUIDE_KO]]: source/docs/history/runtime data 폴더 기준.
- [[docs/COMPLETION_TO_RELEASE_PLAN_KO]]: release까지 남은 큰 축.
- [[docs/PLAN]]: production-readiness master plan.

## Core Architecture
- [[docs/architecture/PROJECT_CONSTITUTION_KO]]: VSM-CSM product constitution.
- [[docs/architecture/VSM_WORKSTATION_ARCHITECTURE_KO]]: VSM workstation runtime spine.
- [[docs/architecture/VSM_TRUTH_FIRST_LOAD_ARCHITECTURE_KO]]: truth-first live analysis and display separation.
- [[docs/architecture/VSM_CAPTURE_CORE_MEMORY_ARCHITECTURE_KO]]: capture-core memory/hot-path 최종 구조.
- [[docs/architecture/VSM_CORE_DATA_VIEW_TAP_ARCHITECTURE_KO]]: Core-owned data plane, view query plane, optional debug tap plane.
- [[docs/architecture/VSM_PASSIVE_SAFE_2PLUS1_ARCHITECTURE_KO]]: Passive Product profile, 2+1 process split, vehicle-impact-free runtime policy.
- [[docs/architecture/VSM_PASSIVE_DEBUG_TAP_PRODUCT_ARCHITECTURE_KO]]: Passive Product non-owning debug tap process, data flow, and USB lifecycle evidence contract.
- [[docs/architecture/VSM_DATA_OWNERSHIP_BOUNDARY_RULES_KO]]: live/capture-core data owner, consumer, drop policy, forbidden boundary rules, static boundary scan.
- [[docs/reviews/passive_product_boundary_audit]]: Passive Product truth taxonomy, scenario audit, capability-claim/proof separation, 2-bus mismatch and quarantine policy.
- [[docs/hardware/CSM_PASSIVE_FRONTEND_REQUIREMENTS]]: CSM passive front-end firmware/hardware requirements.
- [[docs/hardware/CSM_PASSIVE_FRONTEND_ACCEPTANCE]]: analyzer/scope/DTC acceptance requirements for vehicle-impact-free PASS.
- [[docs/hardware/CSM_FIELD_SKU_BOM_RULES]]: field SKU BOM and hardware evidence reference rules.
- [[docs/hardware/CSM_USB_CAN_ISOLATION_POLICY]]: USB/CAN isolation and hotplug disturbance policy.
- [[docs/architecture/VMS_ARCHITECTURE_KO]]: VMS runtime split target.
- [[docs/architecture/TYPED_STREAM_PROTOCOL_V1_KO]]: typed stream v1 human contract.
- [[shared/protocol/typed_stream_v1]]: CSM/VSM shared binary contract.
- [[docs/architecture/CONTROL_EVIDENCE_CONTRACT_KO]]: control success/evidence contract.
- [[docs/interfaces/HW_SW_DATA_CONTRACT_KO]]: legacy packet and typed evidence contract.
- [[docs/interfaces/MODEL_PACK_FORMAT_KO]]: model pack format.

## Runbooks
- [[docs/runbooks/BUILD_AND_VERIFY_KO]]: CMake configure/build/ctest/deploy smoke.
- [[docs/runbooks/VSM_CAPTURE_CORE_MEMORY_VERIFY_KO]]: capture-core memory 30s/10m/1h verification.
- [[docs/runbooks/VSM_HIGH_LOAD_USER_ROUTE_HIL_KO]]: actual VSM user-route high-load HIL.
- [[docs/runbooks/VSM_ANALYSIS_TRUTH_STRESS_HIL_KO]]: analysis truth stress HIL.
- [[docs/runbooks/VSM_DEBUG_GATEWAY_FOUNDATION_KO]]: raw serial crash/hang evidence gateway.
- [[docs/runbooks/VSM_VERIFY_RUNNER_KO]]: official HIL/debug/report launcher catalog.
- [[docs/runbooks/RELEASE_AND_DEPLOY_KO]]: portable release and installer hook.
- [[docs/runbooks/STANDALONE_GIT_WORKFLOW_KO]]: standalone git workflow.

## Harness
- [[HARNESS_MASTER_KO]]: harness redesign and skill boundary rules.
- [[docs/ai_harness/BUILD_VERIFY_POLICY_KO]]: build/test/reporting policy.
- [[docs/ai_harness/AI_WORKFLOW_PHILOSOPHY_KO]]: vertical-slice workflow philosophy.
- [[docs/ai_harness/REGRESSION_MATRIX_KO]]: regression matrix.
- [[.agents/skills/capture-core-memory/SKILL.md]]: capture-core memory workflow.
- [[.agents/skills/typed-evidence/SKILL.md]]: typed evidence/protocol workflow.
- [[.agents/skills/graph-performance/SKILL.md]]: graph performance workflow.
- [[.agents/skills/qt-build-verify/SKILL.md]]: build verification workflow.

## Field And Quality
- [[docs/field/2026-06-16_vsm_analysis_truth_3ho1_transport_timing_diagnosis_KO]]: 3호1 typed transport/timing diagnosis.
- [[docs/field/2026-06-12_vsm_load_hil_summary_KO]]: high-load HIL summary.
- [[docs/field/2026-06-12_vsm_ui_load_fix_report_KO]]: previous UI load fix report.
- [[docs/quality/architecture_map]]
- [[docs/quality/requirements_traceability]]
- [[docs/quality/release_checklist]]
- [[docs/quality/coding_rules]]

## History
- [[history/INDEX]]: old decisions, incidents, cleanup notes.
- [[history/decisions/2026-06-25-capture-core-memory-harness-remodel]]: capture-core memory harness remodel decision.
- [[history/decisions/2026-06-27-vsm-owner-boundary-harness-refactor]]: owner-boundary-first harness and static scan decision.
- [[history/decisions/2026-06-29-vsm-passive-safe-runtime-profile]]: passive-safe runtime profile and vehicle-impact-free default decision.
