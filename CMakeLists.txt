cmake_minimum_required(VERSION 3.19)
project(screenRecorder LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets LinguistTools)

qt_standard_project_setup()

qt_add_executable(screenRecorder
    WIN32 MACOSX_BUNDLE
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    regionselector.h
    regionselector.cpp
)

qt_add_translations(
    TARGETS screenRecorder
    TS_FILES screenRecorder_zh_CN.ts
)

target_link_libraries(screenRecorder
    PRIVATE
        Qt::Core
        Qt::Widgets
)

install(TARGETS screenRecorder
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET screenRecorder
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
