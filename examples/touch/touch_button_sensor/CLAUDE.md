# ESP32-P4 MCP 通知规则

当 esp32-p4 MCP server 可用时，使用以下通知工具。

> Agent 只需调用一个工具，设备端自动处理 LED + 音效 + 舵机组合。

---

## 一、结果通知工具

| 工具 | 含义 | 设备动作 |
|------|------|----------|
| `notify_success` | 完成了 | 绿色 ✓ + success 音效 |
| `notify_error` | 失败了 | 红色 ✗ + error 音效 + 摇头 |
| `notify_waiting` | 等你 | 黄色 🔔 + notification 音效 + 点头 |

### 1. 完成成功 → `notify_success`

**触发场景：**
- 编译/构建/烧录成功
- 代码修改完成
- 用户请求的任务完成
- Git 操作成功

```
CallMcpTool: user-esp32-p4 / notify_success {}
```

### 2. 发生错误 → `notify_error`

**触发场景：**
- 编译/构建失败
- 命令执行失败
- 无法完成用户请求
- 找不到需要的文件/代码

```
CallMcpTool: user-esp32-p4 / notify_error {}
```

### 3. 需要用户 → `notify_waiting`

**触发场景：**
- 需要用户做选择
- 询问用户问题
- 等待用户确认
- 有歧义需要澄清

```
CallMcpTool: user-esp32-p4 / notify_waiting {}
```

---

## 二、工作状态指示器

使用 `led_show_icon` 显示当前工作状态，让用户知道 Agent 正在做什么。

### 状态图标对照表

| 状态 | 图标 | 颜色 | 含义 |
|------|------|------|------|
| 开始长任务 | `gear` | 蓝色 `{r:0, g:100, b:255}` | 正在努力工作 |
| 正在构建 | `gear` | 橙色 `{r:255, g:150, b:0}` | 编译/构建中 |
| 执行命令 | `play` | 青色 `{r:0, g:255, b:200}` | 运行脚本/命令 |
| 网络查询 | `wifi` | 蓝色 `{r:0, g:150, b:255}` | 上网搜索/查文档 |
| 代码分析 | `star` | 紫色 `{r:180, g:0, b:255}` | 搜索/分析代码 |
| 读写文件 | `home` | 白色 `{r:200, g:200, b:200}` | 文件操作 |

### 1. 开始长时间任务 → `gear` (蓝色)

**触发场景：**
- 开始复杂的多步骤任务
- 预计执行时间 > 30 秒的操作

```
CallMcpTool: user-esp32-p4 / led_show_icon {"icon": "gear", "color": {"r": 0, "g": 100, "b": 255}}
```

### 2. 正在构建/编译 → `gear` (橙色)

**触发场景：**
- 执行 `idf.py build`
- 执行 `make` / `cmake`
- 任何编译操作

```
CallMcpTool: user-esp32-p4 / led_show_icon {"icon": "gear", "color": {"r": 255, "g": 150, "b": 0}}
```

### 3. 执行脚本/命令 → `play` (青色)

**触发场景：**
- 运行 shell 命令
- 执行测试脚本
- 运行任何终端命令

```
CallMcpTool: user-esp32-p4 / led_show_icon {"icon": "play", "color": {"r": 0, "g": 255, "b": 200}}
```

### 4. 上网查询 → `wifi` (蓝色)

**触发场景：**
- 使用 WebSearch 搜索
- 使用 WebFetch 获取网页
- 查询在线文档

```
CallMcpTool: user-esp32-p4 / led_show_icon {"icon": "wifi", "color": {"r": 0, "g": 150, "b": 255}}
```

### 5. 代码搜索/分析 → `star` (紫色)

**触发场景：**
- 使用 SemanticSearch 搜索代码
- 使用 Grep 搜索
- 大范围代码分析

```
CallMcpTool: user-esp32-p4 / led_show_icon {"icon": "star", "color": {"r": 180, "g": 0, "b": 255}}
```

---

## 三、进度指示（可选）

对于有明确进度的长任务，可使用 `led_show_progress`。

```
CallMcpTool: user-esp32-p4 / led_show_progress {"percent": 50, "color": {"r": 0, "g": 255, "b": 100}}
```

---

## 四、状态流转示例

典型的长任务状态流：

```
用户请求 → gear(蓝) 开始工作
         → star(紫) 分析代码
         → wifi(蓝) 查文档
         → gear(橙) 开始构建
         → notify_success / notify_error 结束
```

---

## 不需要状态指示的场景

- 简单的单步操作
- 读取单个文件
- 回答简单问题
- 执行时间 < 5 秒的快速操作

---

## 注意事项

- **静默调用**：不要在回复中提及状态指示
- **容错处理**：MCP 不可用时继续执行任务
- **配对使用**：开始工作状态后，必须以结果通知结束
- **及时更新**：工作状态变化时立即更新图标
- **不要刷屏**：相同状态不重复调用
