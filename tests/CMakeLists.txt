add_executable(test_packet_parser
    test_packet_parser.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/PacketParser.cpp
)
target_include_directories(test_packet_parser PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/backend
)
target_link_libraries(test_packet_parser PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME packet_parser COMMAND test_packet_parser)

add_executable(test_model_pack_validator
    test_model_pack_validator.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/ModelPack.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/ModelPackValidator.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/AppLogging.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/RuntimePaths.cpp
)
target_include_directories(test_model_pack_validator PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/backend
)
target_compile_definitions(test_model_pack_validator PRIVATE
    CAN_MONITOR_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
target_link_libraries(test_model_pack_validator PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME model_pack_validator COMMAND test_model_pack_validator)

add_executable(test_ui_state_store
    test_ui_state_store.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/SessionManager.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/UiStateStore.cpp
)
target_include_directories(test_ui_state_store PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/backend
)
target_link_libraries(test_ui_state_store PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME ui_state_store COMMAND test_ui_state_store)

add_executable(test_file_persistence
    test_file_persistence.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/FilePersistence.cpp
)
target_include_directories(test_file_persistence PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/backend
)
target_link_libraries(test_file_persistence PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME file_persistence COMMAND test_file_persistence)

add_executable(test_typed_transport_foundation
    test_typed_transport_foundation.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/FilePersistence.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/RuntimePaths.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/TypedRecords.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/TypedTransportParser.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/StorageRuntime.cpp
)
target_include_directories(test_typed_transport_foundation PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/backend
)
target_compile_definitions(test_typed_transport_foundation PRIVATE
    CAN_MONITOR_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
target_link_libraries(test_typed_transport_foundation PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME typed_transport_foundation COMMAND test_typed_transport_foundation)

add_executable(test_evidence_runtime_foundation
    test_evidence_runtime_foundation.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/TypedRecords.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/evidence/BoardConnectionState.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/evidence/EvidenceRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/evidence/BusRoleResolver.cpp
)
target_include_directories(test_evidence_runtime_foundation PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/backend
)
target_link_libraries(test_evidence_runtime_foundation PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME evidence_runtime_foundation COMMAND test_evidence_runtime_foundation)

add_executable(test_control_audit_model
    test_control_audit_model.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/StableMapListModel.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/control/ControlAuditModel.cpp
)
target_include_directories(test_control_audit_model PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/backend
)
target_link_libraries(test_control_audit_model PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME control_audit_model COMMAND test_control_audit_model)

add_executable(test_control_runtime_foundation
    test_control_runtime_foundation.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/control/ControlRuntime.cpp
)
target_include_directories(test_control_runtime_foundation PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/backend
)
target_link_libraries(test_control_runtime_foundation PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME control_runtime_foundation COMMAND test_control_runtime_foundation)

add_executable(test_transport_runtime_foundation
    test_transport_runtime_foundation.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/FilePersistence.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/AppLogging.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/RuntimePaths.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/PacketParser.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/Recorder.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/ControlCommandEncoder.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/control/ControlSlewLimiter.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/control/ControlCycleRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/SerialWorker.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/TypedIngressRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/HostTxQueue.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/HostTxRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/LegacyIngressRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/LiveProjectionRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/TransportRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/TransportSession.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/TypedRecords.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/TypedTransportParser.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/TypedReplayReader.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/StorageRuntime.cpp
)
target_include_directories(test_transport_runtime_foundation PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/backend
)
target_link_libraries(test_transport_runtime_foundation PRIVATE
    Qt6::Core
    Qt6::SerialPort
    Qt6::Test
)
add_test(NAME transport_runtime_foundation COMMAND test_transport_runtime_foundation)

add_executable(test_typed_replay_reader
    test_typed_replay_reader.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/FilePersistence.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/RuntimePaths.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/TypedRecords.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/TypedTransportParser.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/TypedReplayReader.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/StorageRuntime.cpp
)
target_include_directories(test_typed_replay_reader PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/backend
)
target_compile_definitions(test_typed_replay_reader PRIVATE
    CAN_MONITOR_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
target_link_libraries(test_typed_replay_reader PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME typed_replay_reader COMMAND test_typed_replay_reader)

add_executable(test_storage_replay_runtime_paths
    test_storage_replay_runtime_paths.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/FilePersistence.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/RuntimePaths.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/SessionManager.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/TypedRecords.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/StorageRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/ReplayRuntime.cpp
)
target_include_directories(test_storage_replay_runtime_paths PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/backend
)
target_link_libraries(test_storage_replay_runtime_paths PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME storage_replay_runtime_paths COMMAND test_storage_replay_runtime_paths)

add_executable(test_control_command_encoder
    test_control_command_encoder.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/ControlCommandEncoder.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/control/ControlSlewLimiter.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/control/ControlCycleRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/TypedRecords.cpp
)
target_include_directories(test_control_command_encoder PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/backend
)
target_link_libraries(test_control_command_encoder PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME control_command_encoder COMMAND test_control_command_encoder)

add_executable(test_control_slew_limiter
    test_control_slew_limiter.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/control/ControlSlewLimiter.cpp
)
target_include_directories(test_control_slew_limiter PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/backend
)
target_link_libraries(test_control_slew_limiter PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME control_slew_limiter COMMAND test_control_slew_limiter)

add_executable(test_serial_worker_typed_ingest
    test_serial_worker_typed_ingest.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/FilePersistence.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/AppLogging.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/RuntimePaths.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/PacketParser.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/Recorder.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/ControlCommandEncoder.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/control/ControlSlewLimiter.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/control/ControlCycleRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/SerialWorker.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/TypedIngressRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/HostTxQueue.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/HostTxRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/LegacyIngressRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/LiveProjectionRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/TypedRecords.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/TypedTransportParser.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/TypedReplayReader.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/StorageRuntime.cpp
)
target_include_directories(test_serial_worker_typed_ingest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/backend
)
target_link_libraries(test_serial_worker_typed_ingest PRIVATE
    Qt6::Core
    Qt6::SerialPort
    Qt6::Test
)
add_test(NAME serial_worker_typed_ingest COMMAND test_serial_worker_typed_ingest)

add_executable(test_recorder_flow
    test_recorder_flow.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/FilePersistence.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/AppLogging.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/BuildMetadata.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/RuntimePaths.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/Recorder.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/PacketParser.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/ControlCommandEncoder.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/control/ControlSlewLimiter.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/control/ControlCycleRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/SerialWorker.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/TypedIngressRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/HostTxQueue.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/HostTxRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/LegacyIngressRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/LiveProjectionRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/TypedRecords.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/TypedTransportParser.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/StorageRuntime.cpp
)
target_include_directories(test_recorder_flow PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/backend
)
target_compile_definitions(test_recorder_flow PRIVATE
    CAN_MONITOR_APP_NAME="CAN Monitor Reboot"
    CAN_MONITOR_APP_ID="CAN_Monitor_Reboot"
    CAN_MONITOR_ORG_NAME="CAN Monitor Reboot"
    CAN_MONITOR_ORG_DOMAIN="local.canmonitorreboot"
    CAN_MONITOR_VERSION="0.9.0"
    CAN_MONITOR_BASELINE_ID="turn81_full_buildfix2"
    CAN_MONITOR_BUILD_TIMESTAMP_UTC="test"
    CAN_MONITOR_BUILD_TYPE="Test"
)
target_link_libraries(test_recorder_flow PRIVATE
    Qt6::Core
    Qt6::SerialPort
    Qt6::Test
)
add_test(NAME recorder_flow COMMAND test_recorder_flow)

add_executable(test_replay_engine
    test_replay_engine.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/ReplayEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/PacketParser.cpp
)
target_include_directories(test_replay_engine PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/backend
)
target_link_libraries(test_replay_engine PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME replay_engine COMMAND test_replay_engine)

add_executable(test_analysis_semantics
    test_analysis_semantics.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/SignalDecoder.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/TimingEvaluator.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/FrameListModel.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/FrameFilterProxyModel.cpp
)
target_include_directories(test_analysis_semantics PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/backend
)
target_link_libraries(test_analysis_semantics PRIVATE
    Qt6::Core
    Qt6::Test
)
add_test(NAME analysis_semantics COMMAND test_analysis_semantics)

add_executable(test_app_controller_replay_flow
    test_app_controller_replay_flow.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/BuildMetadata.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/RuntimePaths.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/AppLogging.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/FilePersistence.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/UiStateStore.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/ModelPack.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/ModelPackValidator.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/TimingEvaluator.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/LevelState.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/SignalDecoder.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/AlarmManager.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/PacketParser.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/Recorder.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/FrameListModel.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/SessionManager.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/FrameFilterProxyModel.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/DetailListModel.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/StableMapListModel.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/SerialWorker.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/TypedIngressRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/HostTxQueue.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/HostTxRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/LegacyIngressRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/LiveProjectionRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/TransportRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/TransportSession.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/TypedRecords.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/evidence/BoardConnectionState.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/evidence/EvidenceRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/evidence/BusRoleResolver.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/TypedTransportParser.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/StorageRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/TypedReplayReader.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/control/ControlAuditModel.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/control/ControlRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/ReplayRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/ControlCommandEncoder.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/control/ControlSlewLimiter.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/control/ControlCycleRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/ReplayEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/AppController.cpp
)
target_include_directories(test_app_controller_replay_flow PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/backend
)
target_compile_definitions(test_app_controller_replay_flow PRIVATE
    CAN_MONITOR_APP_NAME="CAN Monitor Reboot"
    CAN_MONITOR_APP_ID="CAN_Monitor_Reboot"
    CAN_MONITOR_ORG_NAME="CAN Monitor Reboot"
    CAN_MONITOR_ORG_DOMAIN="local.canmonitorreboot"
    CAN_MONITOR_VERSION="0.9.0"
    CAN_MONITOR_BASELINE_ID="turn81_full_buildfix2"
    CAN_MONITOR_BUILD_TIMESTAMP_UTC="test"
    CAN_MONITOR_BUILD_TYPE="Test"
)
target_link_libraries(test_app_controller_replay_flow PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::SerialPort
    Qt6::Test
)
add_test(NAME app_controller_replay_flow COMMAND test_app_controller_replay_flow)

add_executable(test_app_controller_log_flow
    test_app_controller_log_flow.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/BuildMetadata.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/RuntimePaths.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/AppLogging.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/FilePersistence.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/UiStateStore.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/ModelPack.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/ModelPackValidator.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/TimingEvaluator.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/LevelState.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/SignalDecoder.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/AlarmManager.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/PacketParser.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/Recorder.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/FrameListModel.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/SessionManager.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/FrameFilterProxyModel.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/DetailListModel.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/StableMapListModel.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/SerialWorker.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/TypedIngressRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/HostTxQueue.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/HostTxRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/LegacyIngressRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/LiveProjectionRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/TransportRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/TransportSession.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/TypedRecords.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/evidence/BoardConnectionState.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/evidence/EvidenceRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/evidence/BusRoleResolver.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/TypedTransportParser.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/StorageRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/TypedReplayReader.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/control/ControlAuditModel.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/control/ControlRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/ReplayRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/ControlCommandEncoder.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/control/ControlSlewLimiter.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/control/ControlCycleRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/ReplayEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/AppController.cpp
)
target_include_directories(test_app_controller_log_flow PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/backend
)
target_compile_definitions(test_app_controller_log_flow PRIVATE
    CAN_MONITOR_APP_NAME="CAN Monitor Reboot"
    CAN_MONITOR_APP_ID="CAN_Monitor_Reboot"
    CAN_MONITOR_ORG_NAME="CAN Monitor Reboot"
    CAN_MONITOR_ORG_DOMAIN="local.canmonitorreboot"
    CAN_MONITOR_VERSION="0.9.0"
    CAN_MONITOR_BASELINE_ID="turn81_full_buildfix2"
    CAN_MONITOR_BUILD_TIMESTAMP_UTC="test"
    CAN_MONITOR_BUILD_TYPE="Test"
)
target_link_libraries(test_app_controller_log_flow PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::SerialPort
    Qt6::Test
)
add_test(NAME app_controller_log_flow COMMAND test_app_controller_log_flow)

add_executable(test_app_controller_analysis_source_flow
    test_app_controller_analysis_source_flow.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/BuildMetadata.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/RuntimePaths.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/AppLogging.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/FilePersistence.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/UiStateStore.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/ModelPack.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/ModelPackValidator.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/TimingEvaluator.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/LevelState.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/SignalDecoder.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/AlarmManager.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/PacketParser.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/Recorder.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/FrameListModel.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/SessionManager.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/FrameFilterProxyModel.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/DetailListModel.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/StableMapListModel.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/SerialWorker.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/TypedIngressRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/HostTxQueue.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/HostTxRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/LegacyIngressRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/LiveProjectionRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/TransportRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/TransportSession.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/TypedRecords.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/evidence/BoardConnectionState.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/evidence/EvidenceRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/evidence/BusRoleResolver.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/TypedTransportParser.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/StorageRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/TypedReplayReader.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/control/ControlAuditModel.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/control/ControlRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/ReplayRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/ControlCommandEncoder.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/control/ControlSlewLimiter.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/control/ControlCycleRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/ReplayEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/AppController.cpp
)
target_include_directories(test_app_controller_analysis_source_flow PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/backend
)
target_compile_definitions(test_app_controller_analysis_source_flow PRIVATE
    CAN_MONITOR_APP_NAME="CAN Monitor Reboot"
    CAN_MONITOR_APP_ID="CAN_Monitor_Reboot"
    CAN_MONITOR_ORG_NAME="CAN Monitor Reboot"
    CAN_MONITOR_ORG_DOMAIN="local.canmonitorreboot"
    CAN_MONITOR_VERSION="0.9.0"
    CAN_MONITOR_BASELINE_ID="turn81_full_buildfix2"
    CAN_MONITOR_BUILD_TIMESTAMP_UTC="test"
    CAN_MONITOR_BUILD_TYPE="Test"
)
target_link_libraries(test_app_controller_analysis_source_flow PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::SerialPort
    Qt6::Test
)
add_test(NAME app_controller_analysis_source_flow COMMAND test_app_controller_analysis_source_flow)

add_executable(test_app_controller_export_snapshot
    test_app_controller_export_snapshot.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/BuildMetadata.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/RuntimePaths.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/AppLogging.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/FilePersistence.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/UiStateStore.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/ModelPack.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/ModelPackValidator.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/TimingEvaluator.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/LevelState.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/SignalDecoder.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/AlarmManager.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/PacketParser.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/Recorder.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/FrameListModel.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/SessionManager.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/FrameFilterProxyModel.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/DetailListModel.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/StableMapListModel.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/SerialWorker.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/TypedIngressRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/HostTxQueue.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/HostTxRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/LegacyIngressRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/LiveProjectionRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/TransportRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/TransportSession.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/TypedRecords.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/evidence/BoardConnectionState.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/evidence/EvidenceRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/evidence/BusRoleResolver.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/TypedTransportParser.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/StorageRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/TypedReplayReader.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/control/ControlAuditModel.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/control/ControlRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/ReplayRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/ControlCommandEncoder.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/control/ControlSlewLimiter.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/control/ControlCycleRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/ReplayEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/AppController.cpp
)
target_include_directories(test_app_controller_export_snapshot PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/backend
)
target_compile_definitions(test_app_controller_export_snapshot PRIVATE
    CAN_MONITOR_APP_NAME="CAN Monitor Reboot"
    CAN_MONITOR_APP_ID="CAN_Monitor_Reboot"
    CAN_MONITOR_ORG_NAME="CAN Monitor Reboot"
    CAN_MONITOR_ORG_DOMAIN="local.canmonitorreboot"
    CAN_MONITOR_VERSION="0.9.0"
    CAN_MONITOR_BASELINE_ID="turn81_full_buildfix2"
    CAN_MONITOR_BUILD_TIMESTAMP_UTC="test"
    CAN_MONITOR_BUILD_TYPE="Test"
)
target_link_libraries(test_app_controller_export_snapshot PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::SerialPort
    Qt6::Test
)
add_test(NAME app_controller_export_snapshot COMMAND test_app_controller_export_snapshot)

add_executable(test_qml_shell_smoke
    test_qml_shell_smoke.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/BuildMetadata.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/RuntimePaths.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/AppLogging.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/FilePersistence.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/UiStateStore.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/ModelPack.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/ModelPackValidator.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/TimingEvaluator.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/LevelState.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/SignalDecoder.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/AlarmManager.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/PacketParser.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/Recorder.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/FrameListModel.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/SessionManager.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/FrameFilterProxyModel.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/DetailListModel.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/StableMapListModel.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/SerialWorker.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/TypedIngressRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/HostTxQueue.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/HostTxRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/LegacyIngressRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/LiveProjectionRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/TransportRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/transport/TransportSession.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/TypedRecords.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/evidence/BoardConnectionState.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/evidence/EvidenceRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/evidence/BusRoleResolver.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/TypedTransportParser.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/StorageRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/TypedReplayReader.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/control/ControlAuditModel.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/control/ControlRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/ReplayRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/ControlCommandEncoder.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/control/ControlSlewLimiter.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/control/ControlCycleRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/ReplayEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/GraphViewportItem.cpp
    ${CMAKE_SOURCE_DIR}/src/backend/AppController.cpp
)
target_include_directories(test_qml_shell_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/backend
)
target_compile_definitions(test_qml_shell_smoke PRIVATE
    CAN_MONITOR_APP_NAME="CAN Monitor Reboot"
    CAN_MONITOR_APP_ID="CAN_Monitor_Reboot"
    CAN_MONITOR_ORG_NAME="CAN Monitor Reboot"
    CAN_MONITOR_ORG_DOMAIN="local.canmonitorreboot"
    CAN_MONITOR_VERSION="0.9.0"
    CAN_MONITOR_BASELINE_ID="turn81_full_buildfix2"
    CAN_MONITOR_BUILD_TIMESTAMP_UTC="test"
    CAN_MONITOR_BUILD_TYPE="Test"
    CAN_MONITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    CAN_MONITOR_QML_DIR="${CMAKE_SOURCE_DIR}/qml"
)
target_link_libraries(test_qml_shell_smoke PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Qml
    Qt6::SerialPort
    Qt6::Test
)
add_test(NAME qml_shell_smoke COMMAND test_qml_shell_smoke)

if (WIN32)
    set(_can_monitor_test_env "PATH=${QT_BIN_DIR};$ENV{PATH}")
    set_tests_properties(packet_parser PROPERTIES ENVIRONMENT "${_can_monitor_test_env}")
    set_tests_properties(model_pack_validator PROPERTIES ENVIRONMENT "${_can_monitor_test_env}")
    set_tests_properties(ui_state_store PROPERTIES ENVIRONMENT "${_can_monitor_test_env}")
    set_tests_properties(file_persistence PROPERTIES ENVIRONMENT "${_can_monitor_test_env}")
    set_tests_properties(typed_transport_foundation PROPERTIES ENVIRONMENT "${_can_monitor_test_env}")
    set_tests_properties(evidence_runtime_foundation PROPERTIES ENVIRONMENT "${_can_monitor_test_env}")
    set_tests_properties(control_audit_model PROPERTIES ENVIRONMENT "${_can_monitor_test_env}")
    set_tests_properties(control_runtime_foundation PROPERTIES ENVIRONMENT "${_can_monitor_test_env}")
    set_tests_properties(transport_runtime_foundation PROPERTIES ENVIRONMENT "${_can_monitor_test_env}")
    set_tests_properties(typed_replay_reader PROPERTIES ENVIRONMENT "${_can_monitor_test_env}")
    set_tests_properties(storage_replay_runtime_paths PROPERTIES ENVIRONMENT "${_can_monitor_test_env}")
    set_tests_properties(control_command_encoder PROPERTIES ENVIRONMENT "${_can_monitor_test_env}")
    set_tests_properties(control_slew_limiter PROPERTIES ENVIRONMENT "${_can_monitor_test_env}")
    set_tests_properties(serial_worker_typed_ingest PROPERTIES ENVIRONMENT "${_can_monitor_test_env}")
    set_tests_properties(recorder_flow PROPERTIES ENVIRONMENT "${_can_monitor_test_env}")
    set_tests_properties(replay_engine PROPERTIES ENVIRONMENT "${_can_monitor_test_env}")
    set_tests_properties(analysis_semantics PROPERTIES ENVIRONMENT "${_can_monitor_test_env}")
    set_tests_properties(app_controller_replay_flow PROPERTIES ENVIRONMENT "${_can_monitor_test_env}")
    set_tests_properties(app_controller_log_flow PROPERTIES ENVIRONMENT "${_can_monitor_test_env}")
    set_tests_properties(app_controller_analysis_source_flow PROPERTIES ENVIRONMENT "${_can_monitor_test_env}")
    set_tests_properties(app_controller_export_snapshot PROPERTIES ENVIRONMENT "${_can_monitor_test_env}")
    set_tests_properties(qml_shell_smoke PROPERTIES ENVIRONMENT "${_can_monitor_test_env}")
endif()
