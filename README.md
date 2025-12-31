# ESP32-S3 音频串口传输系统

基于 **ESP32-S3** + **ES8388** 音频芯片的双向语音传输系统，通过串口实现 PC 与开发板之间的音频数据交换。

![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.x-blue)
![License](https://img.shields.io/badge/License-MIT-green)

---

## ✨ 功能特性

| 功能 | 说明 |
|------|------|
| 🎙️ **录音** | 麦克风采集 → ESP32 → 串口 → PC 保存 WAV |
| 🔊 **播放 WAV** | PC → 串口 → ESP32 → 喇叭播放 |
| 🎵 **播放 MP3** | PC → 串口 → ESP32 硬件解码 → 喇叭播放 |
| 🎛️ **按键控制** | KEY0 开始/停止录音，KEY1 返回空闲 |
| 💡 **LED 指示** | 空闲(灭) / 录音(亮) / 播放(闪烁) |

---

## 🎵 音频参数

| 参数 | 录音 | 播放 (PCM) | 播放 (MP3) |
|------|------|------------|------------|
| 采样率 | 32 kHz | 16 kHz | 自适应 |
| 位宽 | 16 bit | 16 bit | 16 bit |
| 声道 | 单声道 | 单声道→立体声 | 自适应 |
| 串口波特率 | 230400 bps | 230400 bps | 230400 bps |

---

## 🔧 硬件配置

### 开发板
- **主控**: ESP32-S3
- **音频芯片**: ES8388 (I2S 接口)
- **IO 扩展**: XL9555

### 引脚定义

| 功能 | GPIO | 说明 |
|------|------|------|
| I2S_MCLK | 3 | 主时钟 |
| I2S_BCK | 46 | 位时钟 |
| I2S_WS | 9 | 帧同步 |
| I2S_DO | 10 | 数据输出 (播放) |
| I2S_DI | 14 | 数据输入 (录音) |
| UART1_TX | 17 | 音频串口发送 |
| UART1_RX | 18 | 音频串口接收 |
| LED | 1 | 状态指示 |

> ⚠️ **注意**: UART0 用于日志输出，音频传输使用 **UART1** (GPIO17/18)

---

## 🚀 快速开始

### 1. 编译烧录

```bash
# 设置目标芯片
idf.py set-target esp32s3

# 编译
cd d:\Code\36_bootloader
idf.py fullclean
idf.py build
idf.py -p COM11 flash monitor
```

### 2. PC 端工具

安装依赖:
```bash
pip install pyserial pydub
```

使用示例:
```bash
# 监听模式 - 按开发板 KEY0 开始/停止录音
python tools/audio_tool.py COM9 listen -o recording.wav

# 定时录音 10 秒
python tools/audio_tool.py COM9 record -d 10 -o output.wav

# 播放 WAV 文件
python tools/audio_tool.py COM9 play audio.wav

# 播放 MP3 文件 (ESP32 硬件解码)
python tools/audio_tool.py COM9 play input.mp3

# 握手测试
python tools/audio_tool.py COM9 handshake
```

---

## 📁 项目结构

```
├── main/
│   └── main.c                 # 主程序入口
├── components/BSP/
│   ├── ES8388/                # 音频编解码器驱动
│   ├── I2S/                   # I2S 音频接口
│   ├── IIC/                   # I2C 通信
│   ├── KEY/                   # 按键驱动
│   ├── LED/                   # LED 控制
│   ├── UART_AUDIO/            # 串口音频模块
│   │   ├── uart_audio.c/h     # 协议处理 & 录音/播放
│   │   └── mp3_decoder.c/h    # MP3 解码封装
│   └── XL9555/                # IO 扩展芯片
├── tools/
│   └── audio_tool.py          # PC 端命令行工具
└── managed_components/
    └── espressif__esp_audio_codec/  # MP3 解码库
```

---

## 📡 通信协议

### 帧格式

```
| 帧头 (2B) | 命令 (1B) | 长度 (2B) | 数据 (NB) | 校验 (1B) |
| 0xAA 0x55 |    CMD    | LEN_L/H   |   DATA    |    XOR    |
```

### 命令列表

| 命令 | 值 | 方向 | 说明 |
|------|-----|------|------|
| START_RECORD | 0x01 | PC→ESP | 开始录音 |
| STOP_RECORD | 0x02 | PC→ESP | 停止录音 |
| AUDIO_DATA | 0x03 | 双向 | 音频数据包 |
| START_PLAY | 0x04 | PC→ESP | 开始播放 |
| STOP_PLAY | 0x05 | PC→ESP | 停止播放 |
| HANDSHAKE | 0x06 | PC→ESP | 握手请求 |
| ACK | 0x07 | ESP→PC | 应答 |
| SET_FORMAT | 0x08 | PC→ESP | 设置格式 (0=PCM, 1=MP3) |

---

## � 依赖

- **ESP-IDF**: v5.x
- **esp_audio_codec**: ^2.4.0 (MP3 解码)
- **Python**: pyserial, pydub (可选，用于 MP3)

---

## 🔄 工作流程

### 录音流程
```
[麦克风] → [ES8388 ADC] → [I2S RX] → [ESP32 处理] → [UART TX] → [PC 保存 WAV]
```

### 播放流程
```
[PC 读取文件] → [UART RX] → [ESP32 解码] → [I2S TX] → [ES8388 DAC] → [喇叭]
```

---

## 📄 License

MIT License © 2025