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
    systemaudio.h
    systemaudio.cpp
    systemaudiothread.h
    systemaudiothread.cpp
    audiopipe.h
    audiopipe.cpp
    audiotimeline.h
    audiotimeline.cpp
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

if(WIN32)
    # WASAPI 通过 COM 接口和 Windows 的 GUID 定义访问音频设备。
    target_link_libraries(screenRecorder PRIVATE ole32 uuid)
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
