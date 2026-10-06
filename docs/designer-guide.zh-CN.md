# 用 Qt Designer 修改页面

本项目的布局、控件和文案直接存储在 `.ui`。这些是 Qt Designer 原生文件，不需要先运行生成脚本。

| 文件 | 对应页面/组件 | 绑定 C++ |
| --- | --- | --- |
| src/ui/mainwindow.ui | 导航与页面容器 | MainWindow 构造函数 |
| src/ui/todaypage.ui | 今日概览 | MainWindow::createToday |
| src/ui/reminderspage.ui | 提醒列表与操作 | MainWindow::createReminders |
| src/ui/pomodoropage.ui | 番茄钟 | MainWindow::createPomodoro |
| src/ui/historypage.ui | 历史表格 | MainWindow::createHistory |
| src/ui/settingspage.ui | 外观、行为、声音、数据 | MainWindow::createSettings |
| src/ui/remindereditor.ui | 新建/编辑提醒 | ReminderEditor |
| src/ui/ruleeditor.ui | 一条时间规则 | RuleEditor |
| src/ui/reminderpopup.ui | 到期弹窗 | ReminderPopup |

Qt Creator 打开 CMakeLists.txt，选择正确 Kit。在项目树里双击 `.ui`，切换到“设计”模式即可拖放控件、调整布局和预览。CMake AUTOUIC 在构建目录生成 ui_*.h，源码里 include 这些生成头，然后调用 setupUi。**不要手动修改 build 里的 ui_*.h**，下一次构建会覆盖。

每个子页面使用自己的 Ui::Page 类型。setupUi 后，控件由 Qt 的 parent-child 机制持有；局部 Ui 对象只是保存控件指针的辅助结构，它离开作用域不会销毁页面。

## 添加一个按钮

1. 在对应 `.ui` 中拖入 QPushButton。
2. 设置 objectName，例如 exportButton；设置 text。
3. 把按钮加入已有布局，避免绝对坐标。
4. 保存 `.ui`，构建一次，AUTOUIC 生成对应成员。
5. 在 C++ 的页面绑定方法中 connect 信号，调用应用服务。

命名现有控件会影响 C++ 引用，例如 form.addReminder，因此改 objectName 后同步改代码。纯文案、边距、布局比例一般无需更改业务代码。

## 自定义圆环

pomodoropage.ui 中的 countdownRing 是从 QWidget 提升为 qding::CountdownRing 的控件，头文件为 ui/countdownring.h。Designer 不安装自定义插件时显示普通 QWidget 占位；真实程序由 C++ paintEvent 绘制圆环和文字。

提升控件需要可接受 QWidget* parent 的构造函数。ThemeManager 通过 setTheme 注入，番茄钟值通过 setSnapshot 更新。Qt 已处理逻辑坐标与高 DPI，绘制代码不再手动对坐标乘设备像素比。

## 外观与尺寸

使用布局管理器和 sizePolicy。设置页与多规则编辑区域有 QScrollArea，小屏时滚动。正文弹窗使用只读 QTextEdit，长内容可以滚动，不把整个提醒撑出屏幕。

role 和 primary 是 Designer 中的动态属性：role=heading/muted/card/brand 决定对应外观，primary=true 表示强调按钮。主题颜色来自 ThemeManager，QSS 集中维护，避免在每个 `.ui` 中复制颜色。运行时才设置动态属性的控件必要时要重新 polish。

下拉框、数字框与时间框仍是 Designer 的标准控件。`ThemeManager` 集中设置圆角、边距、焦点与按钮区域；`controls.qrc` 打包深浅主题的 SVG 折线箭头，禁用或达到数值边界时显示淡色箭头。Qt SVG 模块负责加载这些资源。

`ui/controlstyle.cpp` 使用 `QProxyStyle` 将下拉框统一为列表，并通过 `QStyledItemDelegate` 绘制圆角选项、柔和高亮和当前值勾选标记。delegate 读取应用调色板，切换主题或强调色立即生效；它不改变控件的键盘、鼠标与数值逻辑。新增标准 QComboBox/QSpinBox/QTimeEdit 会自动使用这套样式，无需逐页复制代码。

建议分别预览浅色、深色、不同字号和 DPI。测试截图命令见 README；源码修改后先重新构建，截图不会自动反映未编译的 `.ui`。

截图目录中的 `rule-options.png` 显示展开的下拉选项，`editor-once.png` 显示带日历按钮的日期时间输入框；`dark/` 目录提供对应深色主题预览。
