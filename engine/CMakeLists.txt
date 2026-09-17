# Engine spike: libvterm + QPainter terminal widget. Built only with
# -DRELAY_BUILD_ENGINE_SPIKE=ON; does not affect the main relay target.
find_package(PkgConfig REQUIRED)
pkg_check_modules(VTERM REQUIRED IMPORTED_TARGET vterm>=0.3)

add_library(relay-vterm STATIC
    Pty.h PtyUnix.cpp
    TerminalBackend.h
    VTermWidget.h VTermWidget.cpp)
target_include_directories(relay-vterm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(relay-vterm PUBLIC ${RELAY_QT}::Widgets PkgConfig::VTERM util)
target_compile_options(relay-vterm PRIVATE -Wall -Wextra)

add_executable(relay-vterm-spike main.cpp)
target_link_libraries(relay-vterm-spike PRIVATE relay-vterm)
