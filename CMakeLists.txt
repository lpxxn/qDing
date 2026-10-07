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
    src/ui/remindereditor.ui src/ui/ruleeditor.ui src/ui/reminderpopup.ui src/ui/celebrationpopup.ui
    src/services/scheduler.h src/services/scheduler.cpp
    src/services/theme.h src/services/theme.cpp
    src/ui/remindermodel.h src/ui/remindermodel.cpp
    src/ui/remindereditor.h src/ui/remindereditor.cpp
    src/ui/popup.h src/ui/popup.cpp
    src/ui/fireworks.h src/ui/fireworks.cpp
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
    # offscreen QPA：无显示器环境跑 Widgets；需要 platforms 插件中的 qoffscreen。
    set_tests_properties(qding_widgets_interactions PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 20)
    add_test(NAME qding_widgets_smoke COMMAND qDing --smoke-test)
    set_tests_properties(qding_widgets_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 20)
endif()

include(GNUInstallDirs)
install(TARGETS qDing BUNDLE DESTINATION . RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
# ---------------------------------------------------------------------------
# Qt 功能插件部署（平台 / SQL / 图像 / 图标引擎 / 多媒体）
#
# qDing 不实现业务插件；运行时由 QApplication、QSqlDatabase、QImageReader、
# QMediaPlayer 等按需加载 Qt 官方插件。详见 docs/qt-plugins.zh-CN.md。
#
# cmake --install 执行下方生成的脚本：Windows=windeployqt，macOS=macdeployqt。
# ---------------------------------------------------------------------------
set(qding_deploy_options "")
if(APPLE)
    # macdeployqt 默认会拷贝整个 SQL 插件组（含 Mimer 等），可能再拖入本机没有的
    # 客户端动态库。应用只使用 QSQLITE，因此：
    #   1) -no-plugins：关闭默认整组拷贝；
    #   2) 按白名单 install 到 Contents/PlugIns/<QT_PLUGIN_TYPE>/；
    #   3) -executable=插件路径：让 macdeployqt 解析该插件自己的 dylib 依赖。
    #
    # 白名单与业务对应：
    #   QCocoa / QOffscreen → 正常 GUI / 测试与打包 smoke（QT_QPA_PLATFORM=offscreen）
    #   QSQLite             → storage.cpp addDatabase("QSQLITE")
    #   Gif/Jpeg/Svg        → 提醒图与资源解码
    #   QSvgIcon            → 托盘等 SVG 图标
    #   Darwin/FFmpeg Media → QMediaPlayer 自定义音频
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
elseif(WIN32)
    # windeployqt 按已链接模块扫描 DLL 与常用插件（含 qwindows、qsqlite、图像等）。
    # 默认往往不带 qoffscreen；而 package-windows.ps1 的部署后检查强制
    # QT_QPA_PLATFORM=offscreen，必须显式纳入，否则干净环境下 smoke 失败。
    list(APPEND qding_deploy_options --include-plugins qoffscreen)
endif()
if(WIN32)
    # qt_generate_deploy_app_script 默认把 plugins/translations 放在安装前缀根；
    # 便携包改为 prefix/bin/plugins 与 prefix/bin/translations（PLUGINS_DIR 相对前缀）。
    qt_generate_deploy_script(
        TARGET qDing
        OUTPUT_SCRIPT deploy_script
        CONTENT "
set(QT_DEPLOY_TRANSLATIONS_DIR \"${CMAKE_INSTALL_BINDIR}/translations\")
qt_deploy_runtime_dependencies(
    EXECUTABLE \"\$<TARGET_FILE:qDing>\"
    GENERATE_QT_CONF
    PLUGINS_DIR ${CMAKE_INSTALL_BINDIR}/plugins
    DEPLOY_TOOL_OPTIONS --include-plugins qoffscreen
)")
else()
    qt_generate_deploy_app_script(TARGET qDing OUTPUT_SCRIPT deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR DEPLOY_TOOL_OPTIONS ${qding_deploy_options})
endif()
install(SCRIPT ${deploy_script})
