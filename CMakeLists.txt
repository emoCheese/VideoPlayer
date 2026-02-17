cmake_minimum_required(VERSION 3.19)
project(QFFPlayer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets OpenGL OpenGLWidgets)
set(CMAKE_AUTOUIC ON)
qt_standard_project_setup()


find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavcodec
    libavformat
    libavutil
    libswscale
)

add_library(glad STATIC glad/src/glad.c)
target_include_directories(glad PUBLIC glad/include)


qt_add_executable(QFFPlayer
    WIN32 MACOSX_BUNDLE
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    VideoWidget/videowidget.cpp VideoWidget/videowidget.h
    Decode/ffmpeg.h Decode/ffmpeg.cpp
    Decode/packetqueue.h Decode/packetqueue.cpp
    Decode/videoplayer.h Decode/videoplayer.cpp
    Decode/framequeue.h Decode/framequeue.cpp
    Decode/demuxer.h Decode/demuxer.cpp
    Decode/videodecoder.h Decode/videodecoder.cpp

    Decode/videoclock.h Decode/videoclock.cpp
    VideoWidget/videorenderercore.h VideoWidget/videorenderercore.cpp
)

target_link_libraries(QFFPlayer
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt6::OpenGL
        Qt6::OpenGLWidgets
        PkgConfig::FFMPEG
        glad
)

target_include_directories(QFFPlayer
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/VideoWidget
    ${CMAKE_CURRENT_SOURCE_DIR}/Decode
    ${CMAKE_CURRENT_SOURCE_DIR}/glad/include
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
