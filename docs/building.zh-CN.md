# 构建、验证与部署

构建命令见 README。CMakePresets 的 debug 使用 Ninja 和 Debug，包含 Qt Test；release 使用 Release 并关闭测试。Qt SDK 的架构要与编译器匹配，Windows 首选 MSVC 2022 x64。

## Qt Creator

打开 CMakeLists.txt；在设置中配置 Qt 版本、编译器和 CMake Kit。macOS 选择 arm64/Apple Clang Kit，Windows 选择 msvc2022_64 Kit，不能把 MinGW Qt 库和 MSVC 编译器混用。

出现 Qt6Config.cmake 找不到时，设置 CMAKE_PREFIX_PATH 为 SDK 根目录，而不是 bin 或某个模块目录。出现 Multimedia 缺失时，用 MaintenanceTool 安装对应 Qt 版本的 Multimedia 模块。

首次配置自动获取固定版本 spdlog。如果离线，先在环境中安装 spdlog 并设置其 CMake 搜索路径。build/_deps 是依赖缓存，不纳入 Git。

## 测试

ctest --preset debug 运行三个目标：

- qding_core_storage：星期/边界/DST、番茄钟、数据库去重/稍后/恢复、宽限与媒体路径。
- qding_widgets_interactions：真实 Designer 编辑器保存多规则，弹窗稍后操作与 GIF 解码，以及定时调度 → 弹窗 → 完成 → 历史落库的完整流程。
- qding_widgets_smoke：临时用户目录中初始化完整程序、保存示例、无声音退出。

界面测试使用 offscreen 平台，不产生正常用户数据库。它能验证控件绑定，但不证明系统托盘、声音设备、休眠/锁屏与登录启动在实机上完全正常。推送后的 GitHub Actions 再执行 macOS 与 Windows SDK 基线构建。

手工检查：每天 10:00、自选周末、工作日周五至周一；休眠/唤醒与锁屏；多个同时到期；关闭到托盘和彻底退出；GIF/自定义声音；不同 DPI、多屏；深浅主题与键盘操作。

## 部署

cmake --install 使用 Qt 的部署脚本，复制动态库、平台插件和媒体依赖。macOS 显式选择 Cocoa/Offscreen、SQLite、GIF/JPEG 与音频插件，避免引入应用不使用的数据库驱动及其外部客户端依赖。Windows 产物位于 build/package/bin，macOS 位于 build/package/qDing.app。不要只复制开发构建的单个可执行文件给其他用户。

在没有安装 Qt 的目标机器上运行，确认 qsqlite 驱动、GIF、音频后端都可用。macOS 首次分发需要签名、公证与稳定安装位置；Windows 正式安装需要签名、启动项和卸载清理。当前 workflow 生成便于检查的部署目录，不等于完成这些发行步骤。

## 日志排查

设置页打开日志目录。spdlog 每份 5 MB，保留 5 份；级别和线程 ID 帮助确认是调度、数据库还是媒体问题。使用 occurrence ID 串联领取、展示与处理。

“没有提醒”先看事项是否启用、规则是否生效、计划日期/星期、勿扰状态与历史结果。missed 表示超过宽限，interrupted 表示上一进程未完成展示，cancelled 表示规则编辑/禁用/删除取消了旧计划。

音频格式能否播放由 Qt 后端决定。MP3/OGG 等失败会回退内置 WAV；设备或系统静音不由应用强制改变。FFmpeg 与平台插件的警告保留在日志中。
