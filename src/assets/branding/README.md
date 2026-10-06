# qDing 应用图标

珊瑚色渐变圆角底座，象牙白铃铛内嵌钟面，右上角一颗暖金色星光。透明边缘适配桌面图标；不使用外部图片、字体或 SVG filter。

- `qding.svg`：可编辑的矢量源文件。
- `qding-*.png`：16–1024 像素的预渲染版本。标准尺寸通过 branding.qrc 用于窗口和 Windows 托盘。
- `qding.icns`：macOS Finder/Dock 图标，含 Retina 尺寸；CMake 复制到 `.app/Contents/Resources` 并设置 `CFBundleIconFile`。
- `qding.ico`：Windows EXE 图标，含 16/24/32/48/64/128/256 像素的 32 位透明帧；`platform/windows.rc` 嵌入 EXE。
- `tray.svg`：macOS 菜单栏单色模板；`QIcon::setIsMask(true)` 让系统适配深浅背景。

所有生成的素材直接随源码提交，普通构建与发布不需要重新生成。页面仍由 `.ui` 定义。

## 修改与重新生成

编辑 `qding.svg` 后，在已配置的 macOS 开发环境运行：

```sh
cmake --build --preset debug --target qding_icon_generator
QT_QPA_PLATFORM=offscreen build/debug/qding_icon_generator \
  src/assets/branding/qding.svg build/icon-preview
cp build/icon-preview/qding-*.png build/icon-preview/qding.ico src/assets/branding/
iconutil -c icns build/icon-preview/qDing.iconset -o src/assets/branding/qding.icns
cmake --build --preset debug
```

生成器使用项目已有的 Qt Gui/Svg，没有 Python 图像依赖。`QSvgRenderer` 渲染透明的 1024 像素母图，缩放得到较小 PNG；`QDataStream` 写入 ICO 的小端目录与 PNG 帧。iconset 包含 macOS 所需的普通/双倍尺寸，系统的 iconutil 转换为 ICNS。

Windows 也能显式构建生成器并生成 PNG/ICO；ICNS 在 macOS 生成后提交即可，无需在 Windows 安装 iconutil。改图标后重新打包，生成的 DMG/ZIP/EXE 会包含新图标。
