# Emotion Game

这是一个基于 Arduino 和 WS2812 LED 灯带的互动情绪/对战装置。两个角色通过压力传感器检测受击和暴击，LED 显示血量、闪烁反馈和背景火焰效果。

## 它做什么

- 读取 4 个模拟输入：两个受击传感器、两个暴击压力传感器。
- 用 LED 灯带显示双方血量。
- 受到攻击时触发掉血和灯光闪烁。
- 游戏结束后自动重置。
- 串口输入 `1` 开启调试，输入 `0` 关闭调试。

## 项目结构

```text
emotion_fight/emotion_fight.ino  Arduino 主程序
model.3dm                        装置/外壳模型文件
*.pdf                            课程设计说明资料
platformio.ini                   PlatformIO 构建配置
```

## Arduino IDE 运行

1. 安装 Arduino IDE。
2. 安装 `FastLED` 库。
3. 打开 `emotion_fight/emotion_fight.ino`。
4. 选择开发板和端口后上传。

## PlatformIO 运行

```bash
pio run
pio run --target upload
```

默认配置使用 `uno`，如果你的板子不是 Arduino Uno，请在 `platformio.ini` 里修改 `board`。

## 硬件连接

- LED 数据引脚：D9
- 角色 1 受击：A0
- 角色 2 受击：A1
- 角色 1 暴击：A2
- 角色 2 暴击：A3
- LED 类型：WS2812，颜色顺序 GRB

## 公开前检查

- 项目不包含 API Key 或 token。
- 这是硬件项目，必须连接传感器和灯带后才能完整验证效果。
