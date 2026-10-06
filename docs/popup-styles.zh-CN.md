# 提醒窗口样式与动画

设置 → 提醒窗口提供两种样式：经典窗口保留原来的 `reminderpopup.ui`；星光烟花使用新的 `celebrationpopup.ui`，有无边框圆角卡片、夜色背景、钟面和短暂烟花。新安装和已有用户均默认使用经典窗口，位置默认右下角。

“在当前屏幕中央显示提醒”适用于两种样式。优先选择鼠标所在屏幕，找不到时使用主屏；位置依据 availableGeometry，避开任务栏/Dock。新卡片根据可用高度调整动画区域和正文区域，长标题、正文与图片可滚动查看。

“开启烟花与入场动画”只作用于新样式。关闭后显示静态星光与钟面；自定义 GIF 仍按原来的媒体规则播放。选项保存在 QSettings 的 `popup/style`、`popup/center`、`popup/animations`，重启后恢复，变更用于下一次提醒及下一次预览。

预览按钮主动打开一个静音窗口，即使当前处于勿扰也可查看。它不创建 occurrence、不播放声音、不写历史。预览的完成/忽略/稍后按钮仅关闭预览。真正到期的提醒以及编辑事项时的预览继续通过 NotificationCoordinator；提醒处理、声音、排队、勿扰、稍后、数据库确认规则与原版本保持一致。

## 两个表单共用处理协议

`ReminderPopup` 构造函数接收一个 `PopupPresentation` 值对象。根据 style 选择 setupUi，通用绑定 lambda 连接两个表单同名的 titleLabel、bodyLabel、imageLabel、snoozeDuration 和动作按钮。没有复制另一套调度或 SQL 逻辑。

完成、稍后、忽略和新窗口关闭按钮都会进入 request。request 发出 action 后禁用操作按钮；真实提醒需存储线程确认，NotificationCoordinator::acknowledge 才关闭窗口；失败会 retry 恢复按钮。关闭按钮和 Escape 都视为忽略。无边框窗口顶部可通过 startSystemMove 拖动。

`WA_TranslucentBackground` 配合圆角 QFrame 与局部 QSS，实现透明边缘；QGraphicsDropShadowEffect 绘制卡片阴影。卡片的配色放在新 `.ui` 的 styleSheet 属性中，可以直接用 Designer 编辑。经典窗口继续跟随应用主题。

`WA_ShowWithoutActivating` 避免显示时抢占输入焦点。macOS 工具窗口额外设置 WA_MacAlwaysShowToolWindow，让应用在后台时也能展示浮层。实际锁屏/勿扰时仍由通知协调器延后展示。

## 烟花如何绘制

`FireworksWidget` 是 Designer 提升控件，头文件 `ui/fireworks.h`。布局由 `.ui` 决定，paintEvent 只负责绘制：

1. 渐变夜空、确定位置的星光和中心钟面。
2. 五组发射与绽放，每组最多 54 个粒子，总上限 270。
3. 用角度、速度、指数减速和重力计算每条轨迹；alpha 随寿命淡出。
4. QPainter 的 Screen 混合模式让暖金、珊瑚、紫色和松石光线叠加。

16 ms 的 QTimer 只触发刷新，实际进度读取 QElapsedTimer 的单调时间；界面偶尔忙碌不会使动画越跑越慢。约 4.2 秒后停止刷新；隐藏时立即停止计时器。用户长时间未处理提醒时保持静态，不持续重播烟花。

进场使用 QParallelAnimationGroup 同时执行 300 ms 的位置缓动与 240 ms 的透明度淡入。隐藏时停止动画并恢复不透明度；offscreen/minimal 测试平台跳过不支持的窗口淡入，仍可检查烟花与布局。全部使用 Qt Widgets/Gui，不使用 Quick、OpenGL、网络素材或新增图像依赖。

## 学习与验证

先阅读 settingspage.ui / MainWindow::createSettings 的设置绑定，再看 popup.h 的值对象和 popup.cpp 的表单选择，最后看 fireworks.cpp 的时间与绘图计算。QPointer 持有样式预览，父窗口持有其生命周期；QMovie 仍以 imageLabel 为父对象。

Widgets 测试覆盖两种样式的稍后确认、到期完成与历史落库、GIF 首帧缩放、居中/无边框标志、设置持久化、勿扰中的静音预览、隐藏时停止烟花。调度回归测试还模拟 GUI 延迟接收扫描结果，确保刚到期事项不会落到 30 秒兜底检查。

README 的截图命令额外生成 `celebration.png`（烟花中的卡片）和 `settings-popup.png`（样式选择），原来的 `popup.png` 继续展示经典窗口。窗口动画与系统级拖动/多屏行为还需在目标平台检查，Windows 构建使用同一套代码。
