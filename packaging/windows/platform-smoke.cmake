# May be included by either the standalone Windows smoke project or the desktop project.
set(RELAY_PLATFORM_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../../src")
add_executable(relay-platform-smoke
  "${CMAKE_CURRENT_LIST_DIR}/platform-smoke.cpp"
  "${RELAY_PLATFORM_SOURCE}/RuntimeDirs.cpp"
  "${RELAY_PLATFORM_SOURCE}/SshConfig.cpp"
  "${RELAY_PLATFORM_SOURCE}/PaneUsage.cpp"
  "${RELAY_PLATFORM_SOURCE}/CrashLog.cpp"
  "${RELAY_PLATFORM_SOURCE}/Logging.cpp"
  "${RELAY_PLATFORM_SOURCE}/SettingsCache.cpp")
target_include_directories(relay-platform-smoke PRIVATE "${RELAY_PLATFORM_SOURCE}")
target_link_libraries(relay-platform-smoke PRIVATE Qt6::Core Psapi)
target_compile_definitions(relay-platform-smoke PRIVATE _WIN32_WINNT=0x0A00 NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
  target_compile_options(relay-platform-smoke PRIVATE /W4 /utf-8)
endif()
add_test(NAME native-platform COMMAND relay-platform-smoke)
set_tests_properties(native-platform PROPERTIES TIMEOUT 30)
