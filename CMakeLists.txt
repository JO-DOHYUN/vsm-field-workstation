cmake_minimum_required(VERSION 3.21)
project(can_monitor_qml_reboot VERSION 0.9.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

option(CAN_MONITOR_ENABLE_POST_BUILD_DEPLOY "Run windeployqt and copy helper files into the build output folder" ON)
option(CAN_MONITOR_ENABLE_TESTS "Build automated unit and component tests" ON)

set(CAN_MONITOR_APP_NAME "CAN Monitor Reboot")
set(CAN_MONITOR_ORG_NAME "CAN Monitor Reboot")
set(CAN_MONITOR_ORG_DOMAIN "local.canmonitorreboot")
set(CAN_MONITOR_BASELINE_ID "workspace-direct-edit" CACHE STRING "Baseline identifier embedded into build metadata")
string(TIMESTAMP CAN_MONITOR_BUILD_TIMESTAMP_UTC "%Y-%m-%dT%H:%M:%SZ" UTC)
string(MAKE_C_IDENTIFIER "${CAN_MONITOR_APP_NAME}" CAN_MONITOR_APP_ID)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Qml SerialPort Network Test)
qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(can_monitor_qml_reboot
    src/main.cpp
    src/backend/CanTypes.h
    src/backend/BuildMetadata.h
    src/backend/BuildMetadata.cpp
    src/backend/RuntimePaths.h
    src/backend/RuntimePaths.cpp
    src/backend/AppLogging.h
    src/backend/AppLogging.cpp
    src/backend/FilePersistence.h
    src/backend/FilePersistence.cpp
    src/backend/TypedRecords.h
    src/backend/TypedRecords.cpp
    src/backend/evidence/BoardConnectionState.h
    src/backend/evidence/BoardConnectionState.cpp
    src/backend/evidence/EvidenceRuntime.h
    src/backend/evidence/EvidenceRuntime.cpp
    src/backend/evidence/BusRoleResolver.h
    src/backend/evidence/BusRoleResolver.cpp
    src/backend/transport/HostTxQueue.h
    src/backend/transport/HostTxQueue.cpp
    src/backend/transport/HostTxRuntime.h
    src/backend/transport/HostTxRuntime.cpp
    src/backend/transport/LegacyIngressRuntime.h
    src/backend/transport/LegacyIngressRuntime.cpp
    src/backend/transport/LiveProjectionRuntime.h
    src/backend/transport/LiveProjectionRuntime.cpp
    src/backend/transport/TransportRuntime.h
    src/backend/transport/TransportRuntime.cpp
    src/backend/transport/TransportSession.h
    src/backend/transport/TransportSession.cpp
    src/backend/transport/TypedIngressRuntime.h
    src/backend/transport/TypedIngressRuntime.cpp
    src/backend/TypedTransportParser.h
    src/backend/TypedTransportParser.cpp
    src/backend/TypedReplayReader.h
    src/backend/TypedReplayReader.cpp
    src/backend/ReplayRuntime.h
    src/backend/ReplayRuntime.cpp
    src/backend/StorageRuntime.h
    src/backend/StorageRuntime.cpp
    src/backend/ControlCommandEncoder.h
    src/backend/ControlCommandEncoder.cpp
    src/backend/control/ControlAuditModel.h
    src/backend/control/ControlAuditModel.cpp
    src/backend/control/ControlRuntime.h
    src/backend/control/ControlRuntime.cpp
    src/backend/control/ControlSlewLimiter.h
    src/backend/control/ControlSlewLimiter.cpp
    src/backend/control/ControlCycleRuntime.h
    src/backend/control/ControlCycleRuntime.cpp
    src/backend/UiStateStore.h
    src/backend/UiStateStore.cpp
    src/backend/PacketParser.h
    src/backend/ModelPack.h
    src/backend/ModelPack.cpp
    src/backend/ModelPackValidator.h
    src/backend/ModelPackValidator.cpp
    src/backend/AnalysisTypes.h
    src/backend/TimingEvaluator.h
    src/backend/TimingEvaluator.cpp
    src/backend/LevelState.h
    src/backend/LevelState.cpp
    src/backend/SignalDecoder.h
    src/backend/SignalDecoder.cpp
    src/backend/AlarmManager.h
    src/backend/AlarmManager.cpp
    src/backend/PacketParser.cpp
    src/backend/Recorder.h
    src/backend/Recorder.cpp
    src/backend/FrameListModel.h
    src/backend/FrameListModel.cpp
    src/backend/SessionManager.h
    src/backend/SessionManager.cpp
    src/backend/FrameFilterProxyModel.h
    src/backend/FrameFilterProxyModel.cpp
    src/backend/DetailListModel.h
    src/backend/DetailListModel.cpp
    src/backend/StableMapListModel.h
    src/backend/StableMapListModel.cpp
    src/backend/SerialWorker.h
    src/backend/SerialWorker.cpp
    src/backend/ReplayEngine.h
    src/backend/ReplayEngine.cpp
    src/backend/AppController.h
    src/backend/AppController.cpp
    src/backend/GraphViewportItem.h
    src/backend/GraphViewportItem.cpp
)

qt_add_qml_module(can_monitor_qml_reboot
    URI CanMonitor
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/pages/OverviewPage.qml
        qml/pages/LivePage.qml
        qml/pages/ReplayPage.qml
        qml/pages/TimingPage.qml
        qml/pages/ValuePage.qml
        qml/pages/GraphPage.qml
        qml/pages/GraphOverviewPage.qml
        qml/pages/AlarmPage.qml
        qml/pages/SettingsPage.qml
        qml/pages/ControlPage.qml
        qml/components/ColumnHeaderCell.qml
        qml/components/StatusBadge.qml
        qml/components/UiMetrics.qml
        qml/components/InfoCard.qml
        qml/components/PageViewport.qml
        qml/components/PreciseSlider.qml
        qml/components/FlowToolbar.qml
        qml/components/EmptyState.qml
        qml/components/DiagnosticStrip.qml
        qml/components/SafeText.qml
        qml/components/SafeButton.qml
        qml/components/SafeCheckBox.qml
        qml/components/SafeTabButton.qml
)


qt_add_resources(can_monitor_qml_reboot "can_monitor_data"
    PREFIX "/"
    FILES
        data/vms_model_R13.json
        data/final_vms_model_R13_rev2.json
        data/model_pack_template.json
        data/vms_signal_map_R13.json
        data/vms_rules_MERGED_R13.json
        data/vms_model_drive_turn77_canzip_final_rev3.json
        data/vms_model_turn77_system_drive_merged_realcan_refresh2_final.json
)

target_include_directories(can_monitor_qml_reboot PRIVATE src src/backend)
target_compile_definitions(can_monitor_qml_reboot
    PRIVATE
        CAN_MONITOR_APP_NAME="${CAN_MONITOR_APP_NAME}"
        CAN_MONITOR_APP_ID="${CAN_MONITOR_APP_ID}"
        CAN_MONITOR_ORG_NAME="${CAN_MONITOR_ORG_NAME}"
        CAN_MONITOR_ORG_DOMAIN="${CAN_MONITOR_ORG_DOMAIN}"
        CAN_MONITOR_VERSION="${PROJECT_VERSION}"
        CAN_MONITOR_BASELINE_ID="${CAN_MONITOR_BASELINE_ID}"
        CAN_MONITOR_BUILD_TIMESTAMP_UTC="${CAN_MONITOR_BUILD_TIMESTAMP_UTC}"
        CAN_MONITOR_BUILD_TYPE="$<CONFIG>"
)

target_link_libraries(can_monitor_qml_reboot
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
        Qt6::Qml
        Qt6::SerialPort
        Qt6::Network
)

if (WIN32)
    target_link_libraries(can_monitor_qml_reboot PRIVATE winmm)
endif()

if (WIN32)
    set_target_properties(can_monitor_qml_reboot PROPERTIES WIN32_EXECUTABLE TRUE)

    get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(QT_BIN_DIR "${QT_QMAKE_EXECUTABLE}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${QT_BIN_DIR}")

    if (CAN_MONITOR_ENABLE_POST_BUILD_DEPLOY AND WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET can_monitor_qml_reboot POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                    --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/qml"
                    --no-translations
                    --compiler-runtime
                    "$<TARGET_FILE:can_monitor_qml_reboot>"
            COMMENT "Deploying Qt runtime next to can_monitor_qml_reboot.exe"
            VERBATIM)
    endif()

    if (CAN_MONITOR_ENABLE_POST_BUILD_DEPLOY)
        add_custom_command(TARGET can_monitor_qml_reboot POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                    "$<TARGET_FILE_DIR:can_monitor_qml_reboot>/data"
            COMMAND ${CMAKE_COMMAND} -E make_directory
                    "$<TARGET_FILE_DIR:can_monitor_qml_reboot>/packaging"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/README_SETUP_KO.md"
                    "$<TARGET_FILE_DIR:can_monitor_qml_reboot>/README_SETUP_KO.md"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/BUILD_FOLDER_USAGE_KO.md"
                    "$<TARGET_FILE_DIR:can_monitor_qml_reboot>/BUILD_FOLDER_USAGE_KO.md"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/deploy_release.bat"
                    "$<TARGET_FILE_DIR:can_monitor_qml_reboot>/deploy_release.bat"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/package_wix_installer.bat"
                    "$<TARGET_FILE_DIR:can_monitor_qml_reboot>/package_wix_installer.bat"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_release_here.bat"
                    "$<TARGET_FILE_DIR:can_monitor_qml_reboot>/run_release_here.bat"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/hil_control_smoke.py"
                    "$<TARGET_FILE_DIR:can_monitor_qml_reboot>/hil_control_smoke.py"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/hil_vsm_user_route_stress.py"
                    "$<TARGET_FILE_DIR:can_monitor_qml_reboot>/hil_vsm_user_route_stress.py"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/analyze_typed_capture.py"
                    "$<TARGET_FILE_DIR:can_monitor_qml_reboot>/analyze_typed_capture.py"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/field_latest_capture_report.py"
                    "$<TARGET_FILE_DIR:can_monitor_qml_reboot>/field_latest_capture_report.py"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/make_release_manifest.py"
                    "$<TARGET_FILE_DIR:can_monitor_qml_reboot>/make_release_manifest.py"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/data/final_vms_model_R13_rev2.json"
                    "$<TARGET_FILE_DIR:can_monitor_qml_reboot>/data/final_vms_model_R13_rev2.json"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/data/vms_rules_MERGED_R13.json"
                    "$<TARGET_FILE_DIR:can_monitor_qml_reboot>/data/vms_rules_MERGED_R13.json"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/data/vms_model_drive_turn77_canzip_final_rev3.json"
                    "$<TARGET_FILE_DIR:can_monitor_qml_reboot>/data/vms_model_drive_turn77_canzip_final_rev3.json"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/data/vms_model_turn77_system_drive_merged_realcan_refresh2_final.json"
                    "$<TARGET_FILE_DIR:can_monitor_qml_reboot>/data/vms_model_turn77_system_drive_merged_realcan_refresh2_final.json"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/SBOM.spdx.json"
                    "$<TARGET_FILE_DIR:can_monitor_qml_reboot>/packaging/SBOM.spdx.json"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/THIRD_PARTY_NOTICES.txt"
                    "$<TARGET_FILE_DIR:can_monitor_qml_reboot>/packaging/THIRD_PARTY_NOTICES.txt"
            COMMENT "Copying local run, deploy, and packaging helper files next to can_monitor_qml_reboot.exe"
            VERBATIM)
    endif()

    add_custom_target(package-portable
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/deploy_release.bat"
                "$<TARGET_FILE_DIR:can_monitor_qml_reboot>"
                "$<TARGET_FILE_DIR:can_monitor_qml_reboot>/portable_release"
        DEPENDS can_monitor_qml_reboot
        COMMENT "Creating portable release folder with deploy_release.bat"
        VERBATIM)

    add_custom_target(package-installer
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/package_wix_installer.bat"
                "$<TARGET_FILE_DIR:can_monitor_qml_reboot>"
                "$<TARGET_FILE_DIR:can_monitor_qml_reboot>/installer_release"
        DEPENDS can_monitor_qml_reboot
        COMMENT "Creating WiX installer artifact when WiX is available"
        VERBATIM)
endif()

install(TARGETS can_monitor_qml_reboot
    BUNDLE DESTINATION .
    RUNTIME DESTINATION .
)

install(FILES
    README_SETUP_KO.md
    BUILD_FOLDER_USAGE_KO.md
    scripts/deploy_release.bat
    scripts/package_wix_installer.bat
    scripts/run_release_here.bat
    scripts/hil_control_smoke.py
    scripts/hil_vsm_user_route_stress.py
    scripts/analyze_typed_capture.py
    scripts/field_latest_capture_report.py
    scripts/make_release_manifest.py
    DESTINATION .
)

install(FILES
    data/final_vms_model_R13_rev2.json
    data/vms_rules_MERGED_R13.json
    data/vms_model_drive_turn77_canzip_final_rev3.json
    data/vms_model_turn77_system_drive_merged_realcan_refresh2_final.json
    DESTINATION data
)

install(FILES
    packaging/SBOM.spdx.json
    packaging/THIRD_PARTY_NOTICES.txt
    DESTINATION packaging
)

if (CAN_MONITOR_ENABLE_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
