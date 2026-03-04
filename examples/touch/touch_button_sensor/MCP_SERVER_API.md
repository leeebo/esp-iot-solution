# ESP32-P4 MCP Server API 文档

## 概述

ESP32-P4 Function EV Board 上运行的 MCP Server，通过 HTTP Transport 暴露硬件控制能力，支持 Cursor、Claude Desktop 等 AI 工具对接。

- **协议版本**: MCP 2024-11-05
- **端点**: `http://<device-ip>/mcp` 或 `http://esp32-mcp.local/mcp`
- **传输**: HTTP POST + JSON-RPC 2.0

## 硬件能力

- **LED 矩阵**: 8x8 WS2812 RGB LED (GPIO 22)
- **音频**: ES8311 Codec 扬声器
- **触摸按钮**: 4 通道电容触摸

---

## 客户端配置

### Cursor

编辑 `~/.cursor/mcp.json`:

```json
{
  "mcpServers": {
    "esp32-p4": {
      "url": "http://esp32-mcp.local/mcp"
    }
  }
}
```

### Claude Desktop

编辑 `~/.config/Claude/claude_desktop_config.json`:

```json
{
  "mcpServers": {
    "esp32-p4": {
      "url": "http://esp32-mcp.local/mcp"
    }
  }
}
```

---

## 工具列表

### 一、LED 预置快捷指令

#### `led.show_number`
显示数字 0-9

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| number | int | 是 | 0-9 |
| color | object | 否 | {r, g, b} 默认红色 |

#### `led.show_char`
显示字符 (0-9, A-Z, a-z)

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| char | string | 是 | 单个字符 |
| color | object | 否 | {r, g, b} 默认绿色 |

#### `led.show_icon`
显示预置图标

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| icon | string | 是 | 图标名称 |
| color | object | 否 | {r, g, b} 默认绿色 |

**可用图标** (24个):
```
check       ✓ 勾选
cross       ✗ 叉号
heart       ♥ 爱心
star        ★ 星星
smile       ☺ 笑脸
sad         ☹ 悲伤
arrow_up    ↑ 上箭头
arrow_down  ↓ 下箭头
arrow_left  ← 左箭头
arrow_right → 右箭头
play        ▶ 播放
pause       ⏸ 暂停
stop        ⏹ 停止
power       ⏻ 电源
wifi        📶 WiFi
bluetooth   🔵 蓝牙
bell        🔔 铃铛
sun         ☀ 太阳
moon        🌙 月亮
music       ♪ 音乐
lock        🔒 锁定
unlock      🔓 解锁
home        🏠 主页
gear        ⚙ 设置
```

#### `led.show_progress`
显示进度条

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| percent | int | 是 | 0-100 |
| color | object | 否 | {r, g, b} 默认绿色 |

#### `led.scroll_text`
滚动显示文字

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| text | string | 是 | 要显示的文字 |
| speed | int | 否 | 滚动速度 50-500ms，默认 100 |
| color | object | 否 | {r, g, b} 默认绿色 |

#### `led.stop_scroll`
停止滚动显示

无参数

---

### 二、LED 基础控制

#### `led.set_color`
全屏填充颜色

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| r | int | 是 | 红色 0-255 |
| g | int | 是 | 绿色 0-255 |
| b | int | 是 | 蓝色 0-255 |

#### `led.set_brightness`
设置全局亮度

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| brightness | int | 是 | 0-255 |

#### `led.clear`
清除 LED 显示

无参数

---

### 三、LED 细粒度像素控制

#### `led.set_pixel`
设置单个像素

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| x | int | 是 | X 坐标 0-7 |
| y | int | 是 | Y 坐标 0-7 |
| r | int | 是 | 红色 0-255 |
| g | int | 是 | 绿色 0-255 |
| b | int | 是 | 蓝色 0-255 |

#### `led.set_pixels`
批量设置多个像素

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| pixels | array | 是 | [{x, y, r, g, b}, ...] |

#### `led.set_bitmap`
设置完整 64 像素位图

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| data | array | 是 | [[r,g,b], ...] 共 64 个 |

#### `led.draw_line`
画直线 (Bresenham 算法)

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| x0, y0 | int | 是 | 起点坐标 0-7 |
| x1, y1 | int | 是 | 终点坐标 0-7 |
| r, g, b | int | 是 | 颜色 0-255 |

#### `led.draw_rect`
画矩形

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| x, y | int | 是 | 左上角坐标 0-7 |
| w, h | int | 是 | 宽高 1-8 |
| r, g, b | int | 是 | 颜色 0-255 |
| fill | int | 否 | 0=边框, 1=填充 |

---

### 四、Audio 预置音效

#### `audio.play_tone`
播放预设音调

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| tone | int | 是 | 1-4 对应 880/988/1047/1319 Hz |

#### `audio.play_sound`
播放预置音效

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| name | string | 是 | 音效名称 |

**可用音效** (10个):

| 名称 | 描述 | 音调 |
|------|------|------|
| success | 成功 | C5-E5-G5 上升三音 |
| error | 错误 | G5-C5 下降两音 |
| warning | 警告 | A5 三连短音 |
| notification | 通知 | C6-G5 叮咚 |
| click | 点击 | 1000Hz 短促 |
| startup | 开机 | C4-E4-G4-C5 上升 |
| shutdown | 关机 | C5-G4-E4-C4 下降 |
| alert | 警报 | A5-B5 交替 |
| confirm | 确认 | G5-G5-C6 |
| cancel | 取消 | C5 长音 |

#### `audio.set_volume`
设置音量

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| volume | int | 是 | 0-100 |

---

### 五、Audio 细粒度控制

#### `audio.play_freq`
播放指定频率

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| freq | int | 是 | 频率 20-20000 Hz |
| duration_ms | int | 是 | 持续时间 50-5000 ms |

#### `audio.play_note`
播放音符

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| note | string | 是 | 音符名 C3-C6 |
| duration_ms | int | 是 | 持续时间 50-5000 ms |

**支持音符**:
```
C3(131Hz)  D3(147Hz)  E3(165Hz)  F3(175Hz)  G3(196Hz)  A3(220Hz)  B3(247Hz)
C4(262Hz)  D4(294Hz)  E4(330Hz)  F4(349Hz)  G4(392Hz)  A4(440Hz)  B4(494Hz)
C5(523Hz)  D5(587Hz)  E5(659Hz)  F5(698Hz)  G5(784Hz)  A5(880Hz)  B5(988Hz)
C6(1047Hz)
```

#### `audio.play_sequence`
播放旋律序列

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| notes | array | 是 | [{note, duration_ms}, ...] |

#### `audio.stop`
停止音频播放

无参数

---

### 六、状态查询

#### `button.get_state`
获取触摸按钮状态

返回: `{button1: bool, button2: bool, button3: bool, button4: bool}`

#### `device.get_status`
获取设备整体状态

返回: `{buttons: [bool,bool,bool,bool], volume: int, led_effect: string}`

---

## 使用示例

### 基础调用格式

```bash
curl -X POST http://esp32-mcp.local/mcp \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "id": 1,
    "method": "tools/call",
    "params": {
      "name": "<tool_name>",
      "arguments": { ... }
    }
  }'
```

### 示例 1: 显示成功提示

```bash
# 显示绿色勾选图标
curl -X POST http://esp32-mcp.local/mcp \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{
    "name":"led.show_icon",
    "arguments":{"icon":"check","color":{"r":0,"g":255,"b":0}}
  }}'

# 播放成功音效
curl -X POST http://esp32-mcp.local/mcp \
  -d '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{
    "name":"audio.play_sound",
    "arguments":{"name":"success"}
  }}'
```

### 示例 2: 滚动显示消息

```bash
curl -X POST http://esp32-mcp.local/mcp \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{
    "name":"led.scroll_text",
    "arguments":{"text":"HELLO WORLD","color":{"r":255,"g":255,"b":0},"speed":80}
  }}'
```

### 示例 3: 像素级绘制

```bash
# 画对角线
curl -X POST http://esp32-mcp.local/mcp \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{
    "name":"led.draw_line",
    "arguments":{"x0":0,"y0":0,"x1":7,"y1":7,"r":255,"g":0,"b":255}
  }}'

# 画填充矩形
curl -X POST http://esp32-mcp.local/mcp \
  -d '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{
    "name":"led.draw_rect",
    "arguments":{"x":2,"y":2,"w":4,"h":4,"r":0,"g":128,"b":255,"fill":1}
  }}'
```

### 示例 4: 自定义旋律

```bash
# 播放 C 大调上行音阶
curl -X POST http://esp32-mcp.local/mcp \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{
    "name":"audio.play_sequence",
    "arguments":{"notes":[
      {"note":"C4","duration_ms":200},
      {"note":"D4","duration_ms":200},
      {"note":"E4","duration_ms":200},
      {"note":"F4","duration_ms":200},
      {"note":"G4","duration_ms":200},
      {"note":"A4","duration_ms":200},
      {"note":"B4","duration_ms":200},
      {"note":"C5","duration_ms":400}
    ]}
  }}'
```

### 示例 5: 显示进度条

```bash
for i in 0 25 50 75 100; do
  curl -s -X POST http://esp32-mcp.local/mcp \
    -d "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{
      \"name\":\"led.show_progress\",
      \"arguments\":{\"percent\":$i,\"color\":{\"r\":0,\"g\":255,\"b\":128}}
    }}"
  sleep 1
done
```

---

## 应用场景

| 场景 | 推荐工具组合 |
|------|-------------|
| 编译完成通知 | `led.show_icon("check")` + `audio.play_sound("success")` |
| 编译失败警告 | `led.show_icon("cross")` + `audio.play_sound("error")` |
| 需要用户确认 | `led.show_icon("bell")` + `audio.play_sound("notification")` |
| 显示任务进度 | `led.show_progress(percent)` |
| 显示倒计时 | `led.show_number(n)` 循环 |
| 滚动状态消息 | `led.scroll_text(message)` |
| 自定义状态指示 | `led.set_pixel()` 或 `led.set_pixels()` |
| 用户输入响应 | `button.get_state()` 轮询 |

---

## 坐标系统

LED 矩阵坐标 (8x8):

```
    0   1   2   3   4   5   6   7   <- X
  +---+---+---+---+---+---+---+---+
0 | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
  +---+---+---+---+---+---+---+---+
1 | 8 | 9 |10 |11 |12 |13 |14 |15 |
  +---+---+---+---+---+---+---+---+
2 |16 |17 |18 |19 |20 |21 |22 |23 |
  +---+---+---+---+---+---+---+---+
3 |24 |25 |26 |27 |28 |29 |30 |31 |
  +---+---+---+---+---+---+---+---+
4 |32 |33 |34 |35 |36 |37 |38 |39 |
  +---+---+---+---+---+---+---+---+
5 |40 |41 |42 |43 |44 |45 |46 |47 |
  +---+---+---+---+---+---+---+---+
6 |48 |49 |50 |51 |52 |53 |54 |55 |
  +---+---+---+---+---+---+---+---+
7 |56 |57 |58 |59 |60 |61 |62 |63 |
  +---+---+---+---+---+---+---+---+
^
Y
```

- 原点 (0,0) 在左上角
- X 轴向右增加 (0-7)
- Y 轴向下增加 (0-7)
- LED 索引 = Y * 8 + X
