cmake_minimum_required(VERSION 3.25)
project(TopBrass VERSION 0.18.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS")
option(TOPBRASS_BUILD_PLUGIN "Build AU/VST3/standalone" ON)
add_library(TopBrassEngine STATIC Source/Engine.cpp Source/Airflow.cpp Source/SectionPlayer.cpp)
target_include_directories(TopBrassEngine PUBLIC Source)
set_target_properties(TopBrassEngine PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(TopBrassEngine PRIVATE -Wall -Wextra -Wpedantic)
add_executable(TopBrassSustainReleaseAudit Tools/SustainReleaseAudit.cpp)
target_link_libraries(TopBrassSustainReleaseAudit PRIVATE TopBrassEngine)
add_executable(TopBrassSelectionBenchmark Tools/SelectionBenchmark.cpp)
target_link_libraries(TopBrassSelectionBenchmark PRIVATE TopBrassEngine)
add_executable(TopBrassBankLoadBenchmark Tools/BankLoadBenchmark.cpp)
target_link_libraries(TopBrassBankLoadBenchmark PRIVATE TopBrassEngine)
add_executable(TopBrassWarTubaBenchmark Tools/WarTubaBenchmark.cpp)
target_link_libraries(TopBrassWarTubaBenchmark PRIVATE TopBrassEngine)
if(APPLE)
    add_executable(TopBrassRenderDeadlineAudit Tools/RenderDeadlineAudit.cpp)
    target_link_libraries(TopBrassRenderDeadlineAudit PRIVATE TopBrassEngine)
    add_executable(TopBrassNativeAudioAudit Tools/NativeAudioAudit.cpp)
    target_link_libraries(TopBrassNativeAudioAudit PRIVATE "-framework AudioToolbox" "-framework CoreAudio" "-framework CoreFoundation")
endif()
include(CTest)
file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/artifacts")
add_executable(TopBrassVerify Tests/Verify.cpp)
target_link_libraries(TopBrassVerify PRIVATE TopBrassEngine)
add_executable(TopBrassNaturalPlayback Tests/NaturalPlayback.cpp)
target_link_libraries(TopBrassNaturalPlayback PRIVATE TopBrassEngine)
add_executable(TopBrassSelectionIndex Tests/SelectionIndex.cpp)
target_link_libraries(TopBrassSelectionIndex PRIVATE TopBrassEngine)
add_test(NAME selection_index COMMAND TopBrassSelectionIndex "${CMAKE_SOURCE_DIR}/assets/TopBrass.bank" "${CMAKE_SOURCE_DIR}/artifacts")
add_executable(TopBrassSampleEnd Tests/SampleEnd.cpp)
target_link_libraries(TopBrassSampleEnd PRIVATE TopBrassEngine)
add_test(NAME sample_end COMMAND TopBrassSampleEnd "${CMAKE_SOURCE_DIR}/artifacts")
add_executable(TopBrassSustainRelease Tests/SustainRelease.cpp)
target_link_libraries(TopBrassSustainRelease PRIVATE TopBrassEngine)
add_test(NAME sustain_release COMMAND TopBrassSustainRelease "${CMAKE_SOURCE_DIR}/assets/TopBrass.bank")
add_executable(TopBrassMappedPlayback Tests/MappedPlayback.cpp)
target_link_libraries(TopBrassMappedPlayback PRIVATE TopBrassEngine)
add_test(NAME mapped_playback COMMAND TopBrassMappedPlayback "${CMAKE_SOURCE_DIR}/artifacts")
add_executable(TopBrassStereoPlayback Tests/StereoPlayback.cpp)
target_link_libraries(TopBrassStereoPlayback PRIVATE TopBrassEngine)
add_test(NAME stereo_playback COMMAND TopBrassStereoPlayback "${CMAKE_SOURCE_DIR}/artifacts")
add_executable(TopBrassKeyNoise Tests/KeyNoise.cpp)
target_link_libraries(TopBrassKeyNoise PRIVATE TopBrassEngine)
add_test(NAME key_noise COMMAND TopBrassKeyNoise "${CMAKE_SOURCE_DIR}/artifacts")
add_executable(TopBrassAirflow Tests/Airflow.cpp)
target_link_libraries(TopBrassAirflow PRIVATE TopBrassEngine)
add_test(NAME airflow COMMAND TopBrassAirflow "${CMAKE_SOURCE_DIR}/artifacts")
add_executable(TopBrassAirflowEngine Tests/AirflowEngine.cpp)
target_link_libraries(TopBrassAirflowEngine PRIVATE TopBrassEngine)
add_test(NAME airflow_engine COMMAND TopBrassAirflowEngine "${CMAKE_SOURCE_DIR}/artifacts")
add_executable(TopBrassPartRouting Tests/PartRouting.cpp)
target_link_libraries(TopBrassPartRouting PRIVATE TopBrassEngine)
add_test(NAME part_routing COMMAND TopBrassPartRouting "${CMAKE_SOURCE_DIR}/assets/TopBrass.bank")
add_executable(TopBrassPlayerTakes Tests/PlayerTakes.cpp)
target_link_libraries(TopBrassPlayerTakes PRIVATE TopBrassEngine)
add_test(NAME player_takes COMMAND TopBrassPlayerTakes "${CMAKE_SOURCE_DIR}/artifacts")
add_executable(TopBrassAlternateTenor Tests/AlternateTenor.cpp)
target_link_libraries(TopBrassAlternateTenor PRIVATE TopBrassEngine)
add_test(NAME alternate_tenor COMMAND TopBrassAlternateTenor "${CMAKE_SOURCE_DIR}/artifacts")
add_executable(TopBrassWarTuba Tests/WarTuba.cpp)
target_link_libraries(TopBrassWarTuba PRIVATE TopBrassEngine)
add_test(NAME war_tuba COMMAND TopBrassWarTuba "${CMAKE_SOURCE_DIR}/artifacts")
add_executable(TopBrassHumanization Tests/Humanization.cpp)
target_link_libraries(TopBrassHumanization PRIVATE TopBrassEngine)
add_test(NAME humanization COMMAND TopBrassHumanization "${CMAKE_SOURCE_DIR}/artifacts" "${CMAKE_SOURCE_DIR}/assets/TopBrass.bank")
add_executable(TopBrassDoubleTongue Tests/DoubleTongue.cpp)
target_link_libraries(TopBrassDoubleTongue PRIVATE TopBrassEngine)
add_test(NAME double_tongue COMMAND TopBrassDoubleTongue "${CMAKE_SOURCE_DIR}/assets/TopBrass.bank")
add_executable(TopBrassSectionPlayer Tests/SectionPlayer.cpp)
target_link_libraries(TopBrassSectionPlayer PRIVATE TopBrassEngine)
add_test(NAME section_player COMMAND TopBrassSectionPlayer)
add_test(NAME natural_playback COMMAND TopBrassNaturalPlayback "${CMAKE_SOURCE_DIR}/assets/TopBrass.bank")
add_test(NAME sample_inputs COMMAND "${CMAKE_SOURCE_DIR}/.venv/bin/python" "${CMAKE_SOURCE_DIR}/Tests/SampleInputs.py")
add_test(NAME install_targets COMMAND "${CMAKE_SOURCE_DIR}/.venv/bin/python" "${CMAKE_SOURCE_DIR}/Tests/InstallTargets.py")
add_test(NAME loop_selection COMMAND "${CMAKE_SOURCE_DIR}/.venv/bin/python" "${CMAKE_SOURCE_DIR}/Tests/LoopSelection.py")
add_test(NAME release_selection COMMAND "${CMAKE_SOURCE_DIR}/.venv/bin/python" "${CMAKE_SOURCE_DIR}/Tests/ReleaseSelection.py")
add_test(NAME recording_audition COMMAND "${CMAKE_SOURCE_DIR}/.venv/bin/python" "${CMAKE_SOURCE_DIR}/Tests/RecordingAudition.py")
add_test(NAME double_tongue_sources COMMAND "${CMAKE_SOURCE_DIR}/.venv/bin/python"
    "${CMAKE_SOURCE_DIR}/scripts/verify_double_tongue_sources.py" "${CMAKE_SOURCE_DIR}/assets/TopBrass.bank"
    "${CMAKE_SOURCE_DIR}/artifacts/double-tongue-sources.json")
add_test(NAME original_sources COMMAND "${CMAKE_SOURCE_DIR}/.venv/bin/python"
    "${CMAKE_SOURCE_DIR}/scripts/verify_original_sources.py" "${CMAKE_SOURCE_DIR}/assets/TopBrass.bank"
    "${CMAKE_SOURCE_DIR}/artifacts/original-sources.json")
add_test(NAME key_sources COMMAND "${CMAKE_SOURCE_DIR}/.venv/bin/python"
    "${CMAKE_SOURCE_DIR}/scripts/verify_key_sources.py" "${CMAKE_SOURCE_DIR}/assets/TopBrass.keys"
    "${CMAKE_SOURCE_DIR}/artifacts/key-sources.json")
add_test(NAME airflow_source COMMAND "${CMAKE_SOURCE_DIR}/.venv/bin/python"
    "${CMAKE_SOURCE_DIR}/scripts/verify_airflow_source.py" "${CMAKE_SOURCE_DIR}/assets/TopBrass.air"
    "${CMAKE_SOURCE_DIR}/artifacts/airflow-source.json")
add_executable(TopBrassTenorDetails Tests/TenorDetails.cpp)
target_link_libraries(TopBrassTenorDetails PRIVATE TopBrassEngine)
add_test(NAME tenor_details COMMAND TopBrassTenorDetails "${CMAKE_SOURCE_DIR}/assets/TopBrass.bank" "${CMAKE_SOURCE_DIR}/assets/TopBrass.tenor" "${CMAKE_SOURCE_DIR}/artifacts")
add_test(NAME tenor_detail_sources COMMAND "${CMAKE_SOURCE_DIR}/.venv/bin/python" "${CMAKE_SOURCE_DIR}/scripts/verify_tenor_detail_sources.py" "${CMAKE_SOURCE_DIR}/assets/TopBrass.tenor" "${CMAKE_SOURCE_DIR}/artifacts/tenor-detail-sources.json")
add_test(NAME engine COMMAND TopBrassVerify "${CMAKE_SOURCE_DIR}/assets/TopBrass.bank" "${CMAKE_SOURCE_DIR}/artifacts" 3263 "${CMAKE_SOURCE_DIR}/assets/TopBrass.keys" "${CMAKE_SOURCE_DIR}/assets/TopBrass.air" "${CMAKE_SOURCE_DIR}/assets/TopBrass.tenor")
if(TOPBRASS_BUILD_PLUGIN)
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/assets/TopBrass.bank" OR NOT EXISTS "${CMAKE_SOURCE_DIR}/assets/TopBrass.keys" OR NOT EXISTS "${CMAKE_SOURCE_DIR}/assets/TopBrass.air" OR NOT EXISTS "${CMAKE_SOURCE_DIR}/assets/TopBrass.tenor")
        message(FATAL_ERROR "Run scripts/bootstrap.sh to prepare the sample resources before building plugins")
    endif()
    add_subdirectory(vendor/JUCE EXCLUDE_FROM_ALL)
    add_executable(TopBrassBellHeader EXCLUDE_FROM_ALL Tools/BellHeader.cpp)
    target_link_libraries(TopBrassBellHeader PRIVATE juce::juce_data_structures)
    target_compile_definitions(TopBrassBellHeader PRIVATE JUCE_USE_CURL=0)
    set(formats VST3 Standalone)
    if(APPLE)
        list(APPEND formats AU)
    endif()
    juce_add_plugin(TopBrass
        COMPANY_NAME "TopBrass"
        BUNDLE_ID "audio.topbrass.instrument"
        PLUGIN_MANUFACTURER_CODE Tpbr
        PLUGIN_CODE Tbrs
        PRODUCT_NAME "TopBrass"
        ICON_BIG "${CMAKE_SOURCE_DIR}/assets/ui/topbrass-logo.png"
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        AU_MAIN_TYPE kAudioUnitType_MusicDevice
        VST3_CATEGORIES Instrument Sampler
        FORMATS ${formats})
    juce_add_binary_data(TopBrassArt SOURCES assets/ui/rank-sprites.png assets/ui/rank-stage.png assets/ui/topbrass-logo.png)
    target_sources(TopBrass PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_compile_definitions(TopBrass PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(TopBrass PRIVATE TopBrassEngine TopBrassArt juce::juce_audio_utils
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
    juce_add_gui_app(TopBrassRecordingAudit PRODUCT_NAME "TopBrass Recording Audit")
    target_sources(TopBrassRecordingAudit PRIVATE Tools/RecordingAudit.cpp)
    target_link_libraries(TopBrassRecordingAudit PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
    target_compile_definitions(TopBrassRecordingAudit PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1)
    if(APPLE)
        target_compile_definitions(TopBrassRecordingAudit PRIVATE JUCE_PLUGINHOST_AU=1)
    endif()
    juce_add_gui_app(TopBrassHostVerify PRODUCT_NAME "TopBrass Host Verify")
    target_sources(TopBrassHostVerify PRIVATE Tests/HostVerify.cpp)
    target_include_directories(TopBrassHostVerify PRIVATE Source)
    target_link_libraries(TopBrassHostVerify PRIVATE TopBrassEngine TopBrassArt juce::juce_audio_utils juce::juce_recommended_config_flags)
    target_sources(TopBrassHostVerify PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_compile_definitions(TopBrassHostVerify PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    add_test(NAME processor COMMAND "$<TARGET_FILE:TopBrassHostVerify>" "${CMAKE_SOURCE_DIR}/artifacts" "${CMAKE_SOURCE_DIR}/scores")
    list(APPEND formats HostVerify)
    file(GLOB sampleNotices CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/assets/plugin-resources/*")
    foreach(format IN LISTS formats)
        if(format STREQUAL "HostVerify")
            set(bundleTarget TopBrassHostVerify)
        else()
            set(bundleTarget TopBrass_${format})
        endif()
        set_property(TARGET ${bundleTarget} APPEND PROPERTY LINK_DEPENDS ${sampleNotices} "${CMAKE_SOURCE_DIR}/assets/TopBrass.bank" "${CMAKE_SOURCE_DIR}/assets/TopBrass.keys" "${CMAKE_SOURCE_DIR}/assets/TopBrass.air" "${CMAKE_SOURCE_DIR}/assets/TopBrass.tenor")
        juce_add_bundle_resources_directory(${bundleTarget} "${CMAKE_SOURCE_DIR}/assets/plugin-resources")
        add_custom_command(TARGET ${bundleTarget} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/assets/TopBrass.bank"
                "$<TARGET_BUNDLE_DIR:${bundleTarget}>/Contents/Resources/TopBrass.bank"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/assets/TopBrass.keys"
                "$<TARGET_BUNDLE_DIR:${bundleTarget}>/Contents/Resources/TopBrass.keys"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/assets/TopBrass.air"
                "$<TARGET_BUNDLE_DIR:${bundleTarget}>/Contents/Resources/TopBrass.air"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/assets/TopBrass.tenor"
                "$<TARGET_BUNDLE_DIR:${bundleTarget}>/Contents/Resources/TopBrass.tenor"
            COMMAND codesign --force --sign - --timestamp=none "$<TARGET_BUNDLE_DIR:${bundleTarget}>"
            VERBATIM)
    endforeach()
endif()
