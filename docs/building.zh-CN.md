# 构建、验证与部署

构建命令见 README。CMakePresets 的 debug 使用 Ninja 和 Debug，包含 Qt Test；release 使用 Release 并关闭测试。Qt SDK 的架构要与编译器匹配，Windows 首选 MSVC 2022 x64。

## Qt Creator

打开 CMakeLists.txt；在设置中配置 Qt 版本、编译器和 CMake Kit。macOS 选择 arm64/Apple Clang Kit，Windows 选择 msvc2022_64 Kit，不能把 MinGW Qt 库和 MSVC 编译器混用。

出现 Qt6Config.cmake 找不到时，设置 CMAKE_PREFIX_PATH 为 SDK 根目录，而不是 bin 或某个模块目录。出现 Multimedia 或 Svg 缺失时，用 MaintenanceTool 安装对应 Qt 版本的 Multimedia、SVG 模块。

首次配置自动获取固定版本 spdlog。如果离线，先在环境中安装 spdlog 并设置其 CMake 搜索路径。build/_deps 是依赖缓存，不纳入 Git。

## 测试

ctest --preset debug 运行三个目标：

- qding_core_storage：星期/边界/DST、番茄钟、数据库去重/稍后/恢复、宽限与媒体路径。
- qding_widgets_interactions：真实 Designer 编辑器保存多规则，两种弹窗的稍后/GIF/完成落库，样式设置与预览，动画生命周期及扫描边界延迟回归。
- qding_widgets_smoke：临时用户目录中初始化完整程序、保存示例、无声音退出。

界面测试使用 offscreen 平台，不产生正常用户数据库。它能验证控件绑定，但不证明系统托盘、声音设备、休眠/锁屏与登录启动在实机上完全正常。推送后的 GitHub Actions 再执行 macOS 与 Windows SDK 基线构建。

手工检查：每天 10:00、自选周末、工作日周五至周一；休眠/唤醒与锁屏；多个同时到期；关闭到托盘和彻底退出；GIF/自定义声音；不同 DPI、多屏；深浅主题与键盘操作。

## 部署

cmake --install 使用 Qt 的部署脚本，复制动态库、平台插件和媒体依赖。macOS 显式选择 Cocoa/Offscreen、SQLite、GIF/JPEG/SVG、SVG 图标与音频插件，避免引入应用不使用的数据库驱动及其外部客户端依赖。Windows 产物位于 build/package/bin，macOS 位于 build/package/qDing.app。不要只复制开发构建的单个可执行文件给其他用户。

在没有安装 Qt 的目标机器上运行，确认 qsqlite 驱动、GIF、音频后端都可用。macOS 首次分发需要签名、公证与稳定安装位置；Windows 正式安装需要签名、启动项和卸载清理。当前 workflow 生成本地分发包，正式证书签名、公证与安装发行流程需另外配置。

## 一键发布脚本

`scripts/package-macos.sh` 与 `scripts/package-windows.ps1` 可从任意文件系统目录调用，源码位置由脚本路径确定。需要 CMake 3.24+、Ninja、Qt 6.8+（含 Multimedia/SVG），首次获取 spdlog 需要网络。macOS 需要 Xcode 命令行工具；Windows 需要 PowerShell 5.1+、Visual Studio 2022 C++ 工具与匹配架构的 MSVC Qt SDK。

流程是：独立的 Release 构建目录 → Qt Test → CMake 安装和 Qt 部署 → 临时数据、无声音的应用检查 → 可选签名 → 压缩包 → SHA-256。脚本会清除应用检查期间的开发机 Qt 插件环境变量，避免遗漏插件却仍能启动。普通构建错误、测试错误、部署或签名错误都会停止脚本；只清理本次创建的临时目录，已生成的发布包不会因这些错误被删除。同版本同架构成功打包时覆盖对应的发布文件。

默认构建目录是 `build/release-macos-<架构>` 或 `build/release-windows-<架构>`，输出在项目 `dist/`。每次配置使用 `cmake --fresh` 清除 CMake 缓存，避免缓存旧 Qt 路径，同时保留依赖下载和可复用的构建文件。脚本只生成本地发布产物，不创建 Git tag，也不上传 GitHub Release。GitHub Actions 使用相同脚本并保存产物。

### macOS

```sh
./scripts/package-macos.sh --qt "$HOME/Qt/6.12.0/macos" --jobs 6
./scripts/package-macos.sh --help

# 可选：目标架构和输出目录；universal 需要 Qt SDK 同时含两个架构
./scripts/package-macos.sh --qt "$HOME/Qt/6.12.0/macos" \
  --arch universal --output "$HOME/Desktop/qDing Releases"
```

可以用 `QT_ROOT` 环境变量代替 `--qt`。`--arch` 接受 arm64、x86_64、universal，默认当前主机架构。测试与应用检查需要主机能运行目标架构的程序，例如 Apple Silicon 上执行 x86_64 程序需要 Rosetta。`--skip-tests` 跳过 Qt Test，但仍保留部署后的启动检查。

```text
dist/
├── qDing-0.1.0-macOS-arm64.dmg
├── qDing-0.1.0-macOS-arm64.zip
└── qDing-0.1.0-macOS-arm64.sha256
```

DMG 中包含 qDing.app、Applications 快捷入口及安装说明。ZIP 保留 `.app` 的完整结构、权限与符号链接。DMG/ZIP 创建后会校验；macOS 默认对应用与内部动态库使用 ad-hoc 临时签名，它不等于 Developer ID 签名或 Apple 公证。

需要正式签名时，先将自己的 Developer ID Application 证书安装到钥匙串，然后显式传入标识：

```sh
./scripts/package-macos.sh --qt "$HOME/Qt/6.12.0/macos" \
  --sign "Developer ID Application: Your Name (TEAMID)"

# 同时公证：先用 notarytool store-credentials 配置自己的钥匙串 profile
./scripts/package-macos.sh --qt "$HOME/Qt/6.12.0/macos" \
  --sign "Developer ID Application: Your Name (TEAMID)" \
  --notary-profile qding-notary
```

传入 `--notary-profile` 后，脚本会将已签名的 app 和 DMG 提交 Apple，分别等待 Accepted，并附加/校验公证票据。拒绝或超时会停止发布。账号密码保存在钥匙串 profile 中，不写入脚本；只提供 `--sign` 不会提交公证。内部文件由内向外签名，外层 app 开启 hardened runtime；正式签名和公证步骤需要你的证书与凭据，当前本机验证使用默认临时签名路径。

### Windows

```powershell
.\scripts\package-windows.ps1 -QtRoot "C:\Qt\6.8.3\msvc2022_64" -Jobs 4
Get-Help .\scripts\package-windows.ps1 -Examples

# 可选输出目录；目标架构默认 x64，亦支持 arm64（需对应工具链/Qt SDK/运行环境）
.\scripts\package-windows.ps1 -QtRoot "C:\Qt\6.8.3\msvc2022_64" -OutputDir "D:\qDing Releases" -SkipTests
```

可使用 `QT_ROOT` 环境变量代替 `-QtRoot`。脚本优先通过 vswhere 找到 Visual Studio，导入 VsDevCmd 的环境；也支持已有的 Developer PowerShell。不会修改系统环境变量。Ninja/CMake 需要在 PATH 中，可使用 Qt Tools 或独立安装。

```text
dist/qDing-0.1.0-Windows-x64.zip
└── qDing-0.1.0-Windows-x64/
    ├── bin/qDing.exe
    ├── bin/Qt6*.dll、平台/图像/数据库/音频插件与运行库
    └── 运行说明.txt
dist/qDing-0.1.0-Windows-x64.sha256
```

这是便携包，完整解压后执行 `bin/qDing.exe`；不生成安装向导、不写启动项。应用检查会等待最多 20 秒；测试或应用非零退出码均视为打包失败。`-SkipTests` 只跳过 Qt Test，程序部署检查仍运行。

如需使用当前用户证书存储中的代码签名证书：

```powershell
.\scripts\package-windows.ps1 -QtRoot "C:\Qt\6.8.3\msvc2022_64" -CertificateThumbprint "你的40位证书SHA1"
```

签名使用 Windows SDK 的 signtool、SHA-256 和 RFC 3161 时间戳；默认时间戳服务为 `http://timestamp.digicert.com`，可用 `-TimestampUrl` 指定。只签应用自身 EXE，保留 Qt/第三方 DLL 的原签名。脚本以 UTF-8 BOM 保存，兼容 PowerShell 5.1 的中文说明。当前 macOS 环境能检查 PowerShell 语法和命令封装，但 Windows 原生构建、部署和证书签名需要 Windows 环境验证。

### 校验下载的包

```sh
# macOS：在 dist 目录执行
shasum -a 256 -c qDing-0.1.0-macOS-arm64.sha256
```

```powershell
# Windows：将输出与 .sha256 文件中的值比较
Get-FileHash .\dist\qDing-0.1.0-Windows-x64.zip -Algorithm SHA256
```

## 日志排查

设置页打开日志目录。spdlog 每份 5 MB，保留 5 份；级别和线程 ID 帮助确认是调度、数据库还是媒体问题。使用 occurrence ID 串联领取、展示与处理。

“没有提醒”先看事项是否启用、规则是否生效、计划日期/星期、勿扰状态与历史结果。missed 表示超过宽限，interrupted 表示上一进程未完成展示，cancelled 表示规则编辑/禁用/删除取消了旧计划。

音频格式能否播放由 Qt 后端决定。MP3/OGG 等失败会回退内置 WAV；设备或系统静音不由应用强制改变。FFmpeg 与平台插件的警告保留在日志中。
