# SQLite、调度与恢复

实际第一版使用五张表：reminders、rules、occurrences、metadata、pomodoro。版本号通过 PRAGMA user_version 保存（不另建迁移表）。媒体元信息暂未单独建 media_assets，引用按受控相对路径保存。后续迁移再增加统计与资源管理表。

## 数据与事务

reminders 记录内容和展示参数；rules 记录多条日程；occurrences 保存一次计划发生的内容快照，删除事项后历史仍能显示原内容。事项逻辑删除，rule_id 在历史中保留文本，不设置会因编辑删除规则而失效的外键。

保存事项、替换规则、取消旧待处理 occurrence 在同一事务执行。相同规则保留 ID/版本，修改时间增加 revision 并从当前时间生效，防止编辑后补发过去时点。重新启用也建立新的生效边界。

绑定值使用 QSqlQuery::prepare/addBindValue。SQLite 的 UNIQUE occurrence_key 防止同一轮重复生成。数据库连接是存储线程的命名连接；退出时释放查询和连接引用后 removeDatabase。外键开启，忙等待 3 秒，首版单连接不启用 WAL。

注意：当前版本编辑任何事项都会取消其尚未处理的展示或稍后请求；编辑后新日程从规则的生效边界继续。历史的 cancelled 记录解释这一行为。

## 调度

ScheduleCalculator::next 查下一次，latest 查一个区间内最近一次。每天/星期规则按当地日期组合时间，不把上次时间简单加 24 小时。默认跟随系统，模型也允许 IANA 时区，当前编辑器只提供系统本地时区。

夏令时不存在的当地时刻跳过；重复时刻选择第一次。occurrence_key 包含规则 ID、revision 和当地日期时间，回拨后再经过同一时刻也不会重复生成。

ReminderScheduler 使用 single-shot QTimer，最多 30 秒检查一次，临近正常日程或稍后时间时缩短等待。数据库中的 last_scan 检查点只和 occurrence 生成在同一事务中提交；失败重试不会因为内存进度已向前移动而漏掉该轮。

数据库 scan 按各规则最近一次生成记录，长期未运行不会逐个枚举旧日程。超过宽限标为 missed；勿扰内标为 deferred。恢复正常后，只领取宽限内的项目。当前 UI 没有为每个事项单独配置错过策略，统一使用设置中的宽限分钟数。

## 展示状态

```text
pending/deferred/snoozed -> claimed -> shown
                                  -> completed/dismissed/snoozed/deferred
宽限外 -> missed
编辑/禁用/删除 -> cancelled
重启遗留 claimed/shown -> interrupted
```

数据库先领取、提交，再发 occurrence 到 GUI。通知协调器每次显示一个窗口，避免声音重叠。用户操作先发存储请求；成功的 occurrenceChanged 才关闭窗口。失败则恢复按钮以便重试。

SQLite 提交与屏幕绘制不是一个原子事务。启动时将遗留 claimed/shown 标记 interrupted，默认不突然再次弹出；历史可查。严格跨崩溃 exactly-once 不作为此版本承诺。

勿扰、锁屏、休眠时，已领取但未处理项转为 deferred；正常展示结束后按原计划时间和宽限重新决定是否展示。稍后提醒使用新的 delivery_at，正常日程不受影响。

## 番茄钟持久化

单一活动番茄钟记录保存在 pomodoro。当前阶段的原始总时长保存在 metadata，和快照一起事务提交，修改配置不会改变当前圆环进度。开始/暂停/继续/停止/切换阶段即时保存，运行中每 30 秒保存快照。正常退出先暂停，再提交剩余时间。非正常终止后，残余误差取决于最近快照，重启总是恢复为暂停。

休眠事件由平台适配通知引擎暂停，锁屏只阻止弹窗，番茄钟仍运行。测试通过 advance 明确推进经过时间，不用等待真实 25 分钟。

## 备份和未来迁移

第一版只提供数据目录入口，没有在线备份按钮。先完全退出程序，再复制 qding.sqlite、settings.ini 和 media。后续数据库迁移必须在事务中检查旧版本，增加 schema 后更新 user_version；不能因打开失败而删除旧数据库。
