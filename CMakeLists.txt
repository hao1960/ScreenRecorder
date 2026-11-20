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
    windowenumerator.h
    windowenumerator.cpp
    windowpickerdialog.h
    windowpickerdialog.cpp
    systemaudio.h
    systemaudio.cpp
    systemaudiothread.h
    systemaudiothread.cpp
    audiopipe.h
    audiopipe.cpp
    audiotimeline.h
    audiotimeline.cpp
    recordingparams.h
    windowtitlebar.h
    windowtitlebar.cpp
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

# 录制参数表只依赖标准库，测试目标不链接 Qt，也不受平台限制。
add_executable(recordingparams_test EXCLUDE_FROM_ALL
    tests/recordingparams_test.cpp
    recordingparams.h
)
set_target_properties(recordingparams_test PROPERTIES AUTOMOC OFF AUTOUIC OFF)
target_compile_features(recordingparams_test PRIVATE cxx_std_17)

if(WIN32)
    # WASAPI 通过 COM 接口和 Windows 的 GUID 定义访问音频设备。
    target_link_libraries(screenRecorder PRIVATE ole32 uuid)
    # 窗口枚举用 DWM 的 cloaked 标记过滤 UWP 隐藏窗口。
    target_link_libraries(screenRecorder PRIVATE dwmapi)
    # 窗口枚举探针：验证 EnumWindows 过滤和 Unicode 标题，不进入主程序。
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/build/window_enum_test.cpp")
        add_executable(window_enum_test EXCLUDE_FROM_ALL
            build/window_enum_test.cpp
        )
        set_target_properties(window_enum_test PROPERTIES AUTOMOC OFF AUTOUIC OFF)
        target_compile_features(window_enum_test PRIVATE cxx_std_17)
        target_link_libraries(window_enum_test PRIVATE user32 dwmapi)
    endif()
    # 窗口选择对话框的独立测试入口；不依赖主窗口，方便单独打开检查列表。
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/build/window_picker_test.cpp")
        add_executable(window_picker_test EXCLUDE_FROM_ALL
            build/window_picker_test.cpp
            windowenumerator.h windowenumerator.cpp
            windowpickerdialog.h windowpickerdialog.cpp
        )
        target_link_libraries(window_picker_test PRIVATE Qt::Core Qt::Widgets user32 dwmapi)
        target_include_directories(window_picker_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    endif()
    # build 下的手动测试不随仓库分发；源码存在时才创建对应目标。
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/build/wasapi_thread_test.cpp")
        add_executable(wasapi_thread_test EXCLUDE_FROM_ALL
            build/wasapi_thread_test.cpp
            systemaudio.h systemaudio.cpp
            systemaudiothread.h systemaudiothread.cpp
            audiopipe.h audiopipe.cpp
            audiotimeline.h audiotimeline.cpp
        )
        target_link_libraries(wasapi_thread_test PRIVATE Qt::Core ole32 uuid)
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/build/wasapi_thread_pipe_test.cpp")
        add_executable(wasapi_thread_pipe_test EXCLUDE_FROM_ALL
            build/wasapi_thread_pipe_test.cpp
            systemaudio.h systemaudio.cpp
            audiopipe.h audiopipe.cpp
            systemaudiothread.h systemaudiothread.cpp
            audiotimeline.h audiotimeline.cpp
        )
        target_link_libraries(wasapi_thread_pipe_test PRIVATE Qt::Core ole32 uuid)
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/build/audio_pipe_connect_test.cpp")
        add_executable(audio_pipe_connect_test EXCLUDE_FROM_ALL
            build/audio_pipe_connect_test.cpp
            audiopipe.h audiopipe.cpp
        )
        target_link_libraries(audio_pipe_connect_test PRIVATE Qt::Core)
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/build/audio_pipe_write_test.cpp")
        add_executable(audio_pipe_write_test EXCLUDE_FROM_ALL
            build/audio_pipe_write_test.cpp
            audiopipe.h audiopipe.cpp
        )
        target_link_libraries(audio_pipe_write_test PRIVATE Qt::Core)
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/build/wasapi_read_test.cpp")
        add_executable(wasapi_read_test EXCLUDE_FROM_ALL
            build/wasapi_read_test.cpp
            systemaudio.h systemaudio.cpp
        )
        set_target_properties(wasapi_read_test PROPERTIES AUTOMOC OFF AUTOUIC OFF)
        target_compile_features(wasapi_read_test PRIVATE cxx_std_17)
        target_link_libraries(wasapi_read_test PRIVATE ole32 uuid)
    endif()
    # 时间线计算只依赖标准 C++，独立检查补帧和重叠处理规则。
    add_executable(audiotimeline_test EXCLUDE_FROM_ALL
        tests/audiotimeline_test.cpp
        audiotimeline.h audiotimeline.cpp
    )
    set_target_properties(audiotimeline_test PROPERTIES AUTOMOC OFF AUTOUIC OFF)
    target_compile_features(audiotimeline_test PRIVATE cxx_std_17)
endif()

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
