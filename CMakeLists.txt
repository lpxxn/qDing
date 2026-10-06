cmake_minimum_required(VERSION 3.24)
project(qDing VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Sql Multimedia Network Svg)
find_package(spdlog CONFIG QUIET)
if(NOT spdlog_FOUND)
    include(FetchContent)
    FetchContent_Declare(spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.15.3
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(spdlog)
endif()

add_library(qding_core
    src/domain/types.h
    src/domain/schedule.h src/domain/schedule.cpp
    src/domain/pomodoro.h src/domain/pomodoro.cpp)
target_include_directories(qding_core PUBLIC src)
target_link_libraries(qding_core PUBLIC Qt6::Core)

add_library(qding_storage
    src/storage/storage.h src/storage/storage.cpp
    src/services/media.h src/services/media.cpp
    src/logging/logging.h src/logging/logging.cpp)
target_link_libraries(qding_storage PUBLIC qding_core Qt6::Sql Qt6::Gui)
if(WIN32)
    # 日志路径可能包含中文用户名。header-only 避免与已安装的窄字符 spdlog ABI 混用。
    target_link_libraries(qding_storage PUBLIC spdlog::spdlog_header_only)
    target_compile_definitions(qding_storage PUBLIC SPDLOG_WCHAR_FILENAMES)
else()
    target_link_libraries(qding_storage PUBLIC spdlog::spdlog)
endif()

set(platform_sources src/platform/integration.cpp)
if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
    list(APPEND platform_sources src/platform/macos.mm)
    set_source_files_properties(src/platform/macos.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
elseif(WIN32)
    list(APPEND platform_sources src/platform/windows.cpp)
endif()

add_library(qding_ui
    src/ui/mainwindow.ui src/ui/todaypage.ui src/ui/reminderspage.ui
    src/ui/pomodoropage.ui src/ui/historypage.ui src/ui/settingspage.ui
    src/ui/remindereditor.ui src/ui/ruleeditor.ui src/ui/reminderpopup.ui
    src/services/scheduler.h src/services/scheduler.cpp
    src/services/theme.h src/services/theme.cpp
    src/ui/remindermodel.h src/ui/remindermodel.cpp
    src/ui/remindereditor.h src/ui/remindereditor.cpp
    src/ui/popup.h src/ui/popup.cpp
    src/ui/mainwindow.h src/ui/mainwindow.cpp
    src/ui/countdownring.h src/ui/countdownring.cpp
    src/ui/controlstyle.h src/ui/controlstyle.cpp src/ui/controls.qrc
    src/platform/integration.h ${platform_sources})
target_link_libraries(qding_ui PUBLIC qding_storage Qt6::Widgets Qt6::Multimedia Qt6::Network Qt6::Svg)
if(APPLE)
    target_link_libraries(qding_ui PRIVATE "-framework AppKit" "-framework ServiceManagement")
elseif(WIN32)
    target_link_libraries(qding_ui PRIVATE Wtsapi32 Advapi32)
endif()

qt_add_executable(qDing WIN32 MACOSX_BUNDLE src/main.cpp)
target_sources(qDing PRIVATE src/assets/branding.qrc)
target_link_libraries(qDing PRIVATE qding_ui)
if(APPLE)
    target_sources(qDing PRIVATE src/assets/branding/qding.icns)
    set_source_files_properties(src/assets/branding/qding.icns PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(qDing PROPERTIES MACOSX_BUNDLE_ICON_FILE qding.icns)
elseif(WIN32)
    enable_language(RC)
    target_sources(qDing PRIVATE src/platform/windows.rc)
    target_include_directories(qDing PRIVATE src/assets/branding)
endif()

# 修改图标时才显式构建/运行此工具；应用构建与发布不依赖它。
add_executable(qding_icon_generator EXCLUDE_FROM_ALL tools/generate-icons.cpp)
target_link_libraries(qding_icon_generator PRIVATE Qt6::Gui Qt6::Svg)
set_target_properties(qDing PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.lpxxn.qding"
    MACOSX_BUNDLE_BUNDLE_NAME "qDing"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")

foreach(target qding_core qding_storage qding_ui qDing qding_icon_generator)
    if(MSVC)
        # rc.exe 不接受 C++ 编译器参数；Windows 原生图标资源使用独立的 RC 语言。
        target_compile_options(${target} PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:/W4>
            $<$<COMPILE_LANGUAGE:CXX>:/permissive->
            $<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(qding_tests tests/core_test.cpp)
    target_link_libraries(qding_tests PRIVATE qding_storage Qt6::Test)
    add_test(NAME qding_core_storage COMMAND qding_tests)
    qt_add_executable(qding_widgets_tests tests/widgets_test.cpp)
    target_link_libraries(qding_widgets_tests PRIVATE qding_ui Qt6::Test)
    add_test(NAME qding_widgets_interactions COMMAND qding_widgets_tests)
    set_tests_properties(qding_widgets_interactions PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 20)
    add_test(NAME qding_widgets_smoke COMMAND qDing --smoke-test)
    set_tests_properties(qding_widgets_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 20)
endif()

include(GNUInstallDirs)
install(TARGETS qDing BUNDLE DESTINATION . RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
set(qding_deploy_options "")
if(APPLE)
    # macdeployqt 默认拷贝整个 SQL 插件组，可能引入没有安装客户端的 Mimer 等驱动。
    # 本应用只用 SQLite；显式选择插件，并让工具追踪每个插件的动态库依赖。
    list(APPEND qding_deploy_options -no-plugins)
    foreach(plugin QCocoaIntegrationPlugin QOffscreenIntegrationPlugin QSQLiteDriverPlugin
            QGifPlugin QJpegPlugin QSvgPlugin QSvgIconPlugin QDarwinMediaPlugin QFFmpegMediaPlugin)
        if(TARGET Qt6::${plugin})
            get_target_property(plugin_type Qt6::${plugin} QT_PLUGIN_TYPE)
            set(plugin_destination "qDing.app/Contents/PlugIns/${plugin_type}")
            install(FILES "$<TARGET_FILE:Qt6::${plugin}>" DESTINATION "${plugin_destination}")
            list(APPEND qding_deploy_options
                "-executable=${plugin_destination}/$<TARGET_FILE_NAME:Qt6::${plugin}>")
        endif()
    endforeach()
endif()
qt_generate_deploy_app_script(TARGET qDing OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR DEPLOY_TOOL_OPTIONS ${qding_deploy_options})
install(SCRIPT ${deploy_script})
