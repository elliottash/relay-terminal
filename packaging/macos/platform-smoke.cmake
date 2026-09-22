# Native Darwin API checks; include after find_package(Qt6 COMPONENTS Core).
set(RELAY_MAC_PLATFORM_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../../src")
add_executable(relay-macos-platform-smoke
  "${CMAKE_CURRENT_LIST_DIR}/platform-smoke.cpp"
  "${RELAY_MAC_PLATFORM_SOURCE}/RuntimeDirs.cpp"
  "${RELAY_MAC_PLATFORM_SOURCE}/SshConfig.cpp"
  "${RELAY_MAC_PLATFORM_SOURCE}/PaneUsage.cpp"
  "${RELAY_MAC_PLATFORM_SOURCE}/CrashLog.cpp"
  "${RELAY_MAC_PLATFORM_SOURCE}/Logging.cpp"
  "${RELAY_MAC_PLATFORM_SOURCE}/SettingsCache.cpp")
target_include_directories(relay-macos-platform-smoke PRIVATE "${RELAY_MAC_PLATFORM_SOURCE}")
target_link_libraries(relay-macos-platform-smoke PRIVATE Qt6::Core)
add_test(NAME macos-platform COMMAND relay-macos-platform-smoke)
set_tests_properties(macos-platform PROPERTIES TIMEOUT 30)
