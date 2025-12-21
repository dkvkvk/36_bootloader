# ESP32-S3 音频串口传输系统

基于 ESP32-S3 和 ES8388 音频芯片的音频串口传输系统，支持录音和播放功能。

## ✨ 功能特性

- **录音模式**：麦克风采集音频 → 串口传输 → PC 存储为 WAV 文件
- **播放模式**：PC 发送音频文件 → 串口传输 → ESP32 解码 → 喇叭播放
- **硬件 MP3 解码**：ESP32 端实时解码 MP3，无需 PC 端转码
- **按键控制**：KEY0 切换录音开始/停止，KEY1 返回空闲
- **LED 指示**：空闲(灭) / 录音(亮) / 播放(闪烁)

## 🎵 支持的音频格式

| 格式 | 解码位置 | 说明 |
|------|----------|------|
| WAV | 无需解码 | 直接传输 PCM 数据 |
| MP3 | ESP32 硬件解码 | 使用 esp_audio_codec 组件 |

## 📊 音频参数

| 参数 | 值 |
|------|-----|
| 采样率 | 16000 Hz (PCM) / 自适应 (MP3) |
| 位宽 | 16 bit |
| 声道 | 单声道 (PCM) / 自适应 (MP3) |
| 波特率 | 921600 bps |

## 🔧 硬件要求

- ESP32-S3 开发板（正点原子）
- ES8388 音频 CODEC
- XL9555 IO 扩展芯片
- USB 转 TTL 模块（连接 UART1）

## 🚀 快速开始

### 编译烧录

```bash
# 设置 ESP-IDF 环境
idf.py set-target esp32s3

# 编译
idf.py build

# 烧录并监视
idf.py -p COM7 flash monitor
```

### PC 端工具

> **注意**: 音频串口需要连接到 **UART1** (TX=GPIO17, RX=GPIO18)，不是 USB 串口

```bash
# 安装依赖
pip install pyserial

# 录音 10 秒
python tools/audio_tool.py COM9 record -d 10 -o output.wav

# 播放 WAV 文件
python tools/audio_tool.py COM9 play input.wav

# 播放 MP3 文件 (ESP32 硬件解码)
python tools/audio_tool.py COM9 play input.mp3

# 监听模式（按开发板按键控制录音）
python tools/audio_tool.py COM9 listen

# 握手测试
python tools/audio_tool.py COM9 handshake
```

## 📁 项目结构

```
├── components/BSP/
│   ├── ES8388/       # 音频 CODEC 驱动
│   ├── I2S/          # I2S 接口
│   ├── IIC/          # I2C 通信
│   ├── KEY/          # 按键驱动
│   ├── LED/          # LED 指示
│   ├── UART_AUDIO/   # 串口音频模块
│   │   ├── uart_audio.c/h  # 串口协议处理
│   │   └── mp3_decoder.c/h # MP3 解码器封装
│   └── XL9555/       # IO 扩展
├── main/
│   ├── main.c            # 主程序
│   └── idf_component.yml # 组件依赖
├── managed_components/
│   └── espressif__esp_audio_codec/  # MP3 解码库
└── tools/
    └── audio_tool.py     # PC 端工具
```

## 📡 串口协议

```
帧头(2B) | 命令(1B) | 长度(2B) | 数据(NB) | 校验(1B)
0xAA 0x55 | CMD      | LEN_L/H  | DATA     | XOR
```

**命令定义**：

| 命令 | 值 | 说明 |
|------|-----|------|
| CMD_START_RECORD | 0x01 | 开始录音 |
| CMD_STOP_RECORD | 0x02 | 停止录音 |
| CMD_AUDIO_DATA | 0x03 | 音频数据包 |
| CMD_START_PLAY | 0x04 | 开始播放 |
| CMD_STOP_PLAY | 0x05 | 停止播放 |
| CMD_HANDSHAKE | 0x06 | 握手 |
| CMD_ACK | 0x07 | 应答 |
| CMD_SET_FORMAT | 0x08 | 设置音频格式 (0=PCM, 1=MP3) |

## 📌 引脚定义

| 功能 | GPIO |
|------|------|
| I2S_BCK | 46 |
| I2S_WS | 9 |
| I2S_DO | 10 |
| I2S_DI | 14 |
| I2S_MCLK | 3 |
| UART1_TX (音频) | 17 |
| UART1_RX (音频) | 18 |
| LED | 1 |
| KEY0-3 | XL9555 IO扩展 |

## 📦 依赖组件

- `espressif/esp_audio_codec ^2.4.0` - 音频编解码库
- `espressif/esp_tinyusb ^1` - USB 支持

## License

idf.py fullclean
idf.py build
idf.py -p COM7 flash monitor


Copyright © 2025