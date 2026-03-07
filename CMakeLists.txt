cmake_minimum_required(VERSION 3.19)
project(QFFPlayer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_WIN32_EXECUTABLE OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets)
find_package(Qt6 6.5 REQUIRED COMPONENTS OpenGL OpenGLWidgets)
find_package(PkgConfig REQUIRED)
find_package(fmt REQUIRED)
find_package(spdlog REQUIRED)
find_package(SDL3 REQUIRED)

qt_standard_project_setup()

pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavcodec
    libavformat
    libavutil
    libswscale
    libswresample
)

# 在 release 时加上 WIN32, WIN32 会阻碍 stdout 输出
qt_add_executable(QFFPlayer
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui

    Decode/ffmpeg.h Decode/ffmpeg.cpp
    Decode/packetqueue.h Decode/packetqueue.cpp
    Decode/videoplayer.h Decode/videoplayer.cpp
    Decode/framequeue.h Decode/framequeue.cpp
    Decode/demuxer.h Decode/demuxer.cpp
    Decode/videodecoder.h Decode/videodecoder.cpp


    VideoWidget/videowidget.h VideoWidget/videowidget.cpp
    Decode/Clock/masterclock.h Decode/Clock/masterclock.cpp
    Decode/audiodecoder.h Decode/audiodecoder.cpp

    Decode/Clock/IClockSource.h
    Decode/Clock/videoclock.h Decode/Clock/videoclock.cpp
    Decode/Clock/externalclock.h Decode/Clock/externalclock.cpp
    Decode/AVFrameHolder.h


    Audio/AudioOuput.h Audio/AudioOuput.cpp
    Audio/audioclock.h Audio/audioclock.cpp
)

target_link_libraries(QFFPlayer
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt6::OpenGL
        Qt6::OpenGLWidgets
        PkgConfig::FFMPEG
        fmt::fmt
        spdlog::spdlog_header_only
        SDL3::SDL3-shared
)

target_compile_definitions(QFFPlayer PRIVATE
    $<$<CONFIG:Debug>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE>
    $<$<CONFIG:Release>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_OFF>
)

target_include_directories(QFFPlayer
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/VideoWidget
    ${CMAKE_CURRENT_SOURCE_DIR}/Decode
    ${CMAKE_CURRENT_SOURCE_DIR}/Decode/Clock
    ${CMAKE_CURRENT_SOURCE_DIR}/Audio
)

message(STATUS "FFmpeg include dirs: ${FFMPEG_INCLUDE_DIRS}")
message(STATUS "FFmpeg libraries   : ${FFMPEG_LIBRARIES}")


include(GNUInstallDirs)

install(TARGETS QFFPlayer
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET QFFPlayer
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
