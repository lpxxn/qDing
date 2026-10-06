# qDing

C++20 / Qt Widgets 本地提醒与番茄钟。界面由 **9 个 Qt Designer `.ui` 文件**定义，业务逻辑使用 C++。数据保存在当前用户的本地目录，无需数据库服务器或账号。

## 已实现

- 主窗口关闭后隐藏到托盘；托盘菜单打开、新建提醒、开始/暂停番茄钟、勿扰、退出。
- 单实例；重复启动唤起已有窗口；可选择启动时隐藏到托盘与登录启动。
- 多事项、多时间规则：仅一次、每天、周一至周五、自选星期；支持搜索、编辑、启停、复制和逻辑删除。
- 提醒弹窗：自定义标题和内容、PNG/JPEG/GIF、音频、音量、预览、完成、忽略、稍后 5/10/15 分钟、停止声音。
- 番茄钟：工作/休息时长自定义，自动循环、暂停、继续、停止；默认 25/5 分钟；重启恢复为暂停。
- 勿扰与锁屏延后展示；唤醒补提醒；同一轮去重；SQLite 触发历史；上次中断明确记录。
- 浅色、深色、跟随系统和四种强调色；spdlog 日志轮转；存储线程与界面线程分开。

Windows 登录启动使用当前用户启动项；macOS 使用 SMAppService，需要系统批准，正式发布应使用安装在稳定位置且签名的 `.app`。电脑关闭/休眠、程序完全退出时不执行自定义弹窗。工作日定义为周一至周五，不包含法定节假日调休。

当前版本没有云同步、月/年规则、法定节假日、系统通知中心集成、长休息、任意主题 JSON 导入、历史保留期清理和自动备份。这些是设计方案中的后续扩展。历史页面显示最近 300 条，数据库记录保留；媒体导入后复制到应用目录，暂不自动清理未引用资源。

## 环境与构建

需要 CMake 3.24+、Ninja、C++20 工具链及 Qt 6.8+，安装 Qt Multimedia 和 Qt SVG 模块。macOS 使用 Xcode/Apple Clang，Windows 使用与 Qt SDK 匹配的 MSVC 2022 x64 工具链。spdlog 1.15.3 优先查找本地安装，否则 CMake 从固定 tag 获取，首次构建需网络。

macOS 示例（将 Qt 路径换成自己的安装位置）：

```sh
cmake --preset debug -DCMAKE_PREFIX_PATH="$HOME/Qt/6.12.0/macos"
cmake --build --preset debug --parallel 6
ctest --preset debug
open build/debug/qDing.app
```

Windows 在 **x64 Native Tools Command Prompt for VS 2022** 或 Qt Creator 的 MSVC Kit 中：

```bat
cmake --preset debug -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build --preset debug --parallel 4
ctest --preset debug
build\debug\qDing.exe
```

如果 Qt Creator 已配置正确 Kit，打开 CMakeLists.txt 后可以直接配置、构建和运行。固定表单在设计模式中编辑 `.ui`；动态时间规则行也有独立的 `.ui`。

## 使用与数据

点击“新建”输入标题/内容，添加一个或多个时间规则，按需要选图片和声音，先预览后保存。工作日提醒选择“周一至周五”。关闭主窗口继续后台运行；彻底结束请在托盘菜单中选“退出”。托盘不可用时关闭窗口会退出，避免隐藏后找不到程序。

设置页可修改主题、启动方式、勿扰、补提醒宽限和总音量。默认错过 10 分钟内补提醒；同一规则长时间未运行只生成最近一次结果，避免大量弹窗。同一时间的提醒依次展示，声音不会并发播放。

默认数据目录通过 QStandardPaths::AppLocalDataLocation 获取，设置页可打开目录：

```text
<本地数据目录>/
├── qding.sqlite       # 业务数据与番茄钟快照
├── settings.ini       # QSettings 偏好
├── instance.lock      # 进程互斥
├── media/             # 复制导入的媒体，按内容哈希命名
└── logs/              # 每份 5 MB，保留 5 份
```

手动备份时先从托盘退出，然后复制整个数据目录；不要只复制正在使用中的数据库文件。恢复时同样先退出。程序升级遇到新版本数据库会拒绝打开，避免旧程序破坏新结构。

## 检查与发布

```sh
# 使用临时目录、无声音检查 UI，不写入正常用户数据
QT_QPA_PLATFORM=offscreen build/debug/qDing.app/Contents/MacOS/qDing --smoke-test

# 额外保存页面、编辑器和弹窗截图
QT_QPA_PLATFORM=offscreen build/debug/qDing.app/Contents/MacOS/qDing \
  --smoke-test --screenshot-dir build/screenshots

# 部署包含 Qt 依赖的安装目录；macOS 产物是 .app，Windows 为 bin/qDing.exe
cmake --install build/debug --prefix build/package
```

CLI 还支持 `--background`、`--data-dir <目录>`、`--help`、`--version`。不同数据目录有独立单实例锁。GitHub Actions 配置 macOS 与 Windows 的 Qt 6.8.3 构建、测试与部署；推送后才会实际运行，仓库内的 workflow 不表示 Windows 已在本机验证。

### 一键打包 Release

macOS：

```sh
./scripts/package-macos.sh --qt "$HOME/Qt/6.12.0/macos"
```

Windows PowerShell（使用 MSVC 版本 Qt；脚本自动加载 Visual Studio C++ 环境）：

```powershell
.\scripts\package-windows.ps1 -QtRoot "C:\Qt\6.8.3\msvc2022_64"
```

两者默认构建 Release、运行测试、部署 Qt 依赖并独立检查部署后的程序。输出在 `dist/`：macOS 为 DMG、ZIP 和 SHA-256；Windows 为便携 ZIP 和 SHA-256。版本号读取 CMake，不需要在脚本里同步修改。macOS 默认使用临时签名，可选 Developer ID 签名与 Apple 公证；Windows 可选 Authenticode 签名。参数、输出结构及签名示例见[构建与发布文档](docs/building.zh-CN.md#一键发布脚本)。

## 学习入口

- [实现与选型方案](docs/implementation-plan.zh-CN.md)：完整设计与后续路线。
- [代码导读](docs/code-guide.zh-CN.md)：对象、线程、信号槽、Model/View、计时与所有权。
- [Designer 页面修改](docs/designer-guide.zh-CN.md)：9 个 `.ui` 与自定义控件提升。
- [存储与调度](docs/storage-scheduling.zh-CN.md)：事务、去重、补提醒与崩溃边界。
- [构建与发布](docs/building.zh-CN.md)：两平台工具链、测试、部署、日志排查。
- [应用图标](src/assets/branding/README.md)：矢量源文件、原生 ICNS/ICO 与素材生成方法。

当前工作区已在 macOS arm64 / Qt 6.12.0 上编译、测试和检查 UI。Windows 原生电源/登录启动适配已编写，Windows 实机与 CI 验证需在相应环境运行。发布给其他用户前，检查 Qt 与 spdlog 许可证、macOS 签名/公证及 Windows 安装卸载流程。
