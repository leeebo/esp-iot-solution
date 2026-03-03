## 通过 MCP 对接端侧设备需求

1. 基于当前工程，添加 WiFi 上网支持（目前 target 是 P4，已经挂在了一个 C6 的模块，可以支持 WiFi）
2. 添加 MCP Server ，将硬件的功能原子化 ，例如 LED 矩阵的显示，音频的播放，按键的状态。支持 cursor，claude desktop 等对接，给出详细方案（包括 claude desktop 的对接方案）
3. 未来 cursor 这些工具可以接入 MCP Server，通过控制硬件给我发送状态通知（例如编程完成时，给我一个 LED 矩阵的显示，和音频的播放。需要我批准时候，给我另外一种显示，表示我需要批准）

## 自主迭代

* IDF_PATH 为 /home/libo/esp/gitlab_release_v5_5
* target 为 esp32p4，当前端口是 /dev/ttyACM0
* 测试 WiFi SSID HAPPYBOY, 密码 43539100

