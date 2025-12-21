#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ESP32-S3 音频串口传输工具

功能：
1. 接收 ESP32 录音数据并保存为 WAV 文件
2. 发送 WAV/MP3 音频文件到 ESP32 播放（MP3 会自动转换为 PCM 格式）

协议格式：
帧头(2B) | 命令(1B) | 数据长度(2B) | 数据(NB) | 校验(1B)
0xAA 0x55 | CMD      | LEN_L LEN_H  | DATA...  | XOR

命令定义：
0x01: 开始录音
0x02: 停止录音
0x03: 音频数据包
0x04: 开始播放
0x05: 停止播放
0x06: 握手
0x07: 应答
"""

import serial
import struct
import wave
import time
import argparse
import threading
from pathlib import Path

# MP3 支持
try:
    from pydub import AudioSegment
    PYDUB_AVAILABLE = True
except ImportError:
    PYDUB_AVAILABLE = False
    print("警告: pydub 未安装，MP3 支持不可用。安装方法: pip install pydub")

# 命令定义
CMD_START_RECORD = 0x01
CMD_STOP_RECORD = 0x02
CMD_AUDIO_DATA = 0x03
CMD_START_PLAY = 0x04
CMD_STOP_PLAY = 0x05
CMD_HANDSHAKE = 0x06
CMD_ACK = 0x07
CMD_SET_FORMAT = 0x08  # 新增：设置音频格式

# 音频格式定义
AUDIO_FORMAT_PCM = 0x00
AUDIO_FORMAT_MP3 = 0x01

# 音频参数
SAMPLE_RATE = 8000  # 与 ESP32 I2S 配置一致 (8kHz)
BITS_PER_SAMPLE = 16
CHANNELS = 1

# 帧头
FRAME_HEADER = bytes([0xAA, 0x55])


class AudioSerialTool:
    def __init__(self, port, baudrate=230400):
        self.port = port
        self.baudrate = baudrate
        self.serial = None
        self.running = False
        self.audio_data = bytearray()
        self.rx_thread = None
        
    def connect(self):
        """连接串口"""
        try:
            self.serial = serial.Serial(
                port=self.port,
                baudrate=self.baudrate,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=0.1,
                write_timeout=1
            )
            print(f"已连接到 {self.port}, 波特率: {self.baudrate}")
            return True
        except Exception as e:
            print(f"连接失败: {e}")
            return False
    
    def disconnect(self):
        """断开串口"""
        self.running = False
        if self.rx_thread:
            self.rx_thread.join(timeout=1)
        if self.serial:
            self.serial.close()
            self.serial = None
        print("已断开连接")
    
    def calc_checksum(self, data):
        """计算校验和"""
        checksum = 0
        for b in data:
            checksum ^= b
        return checksum
    
    def send_frame(self, cmd, data=b''):
        """发送帧"""
        if not self.serial:
            return False
        
        length = len(data)
        header = struct.pack('<2sBH', FRAME_HEADER, cmd, length)
        checksum = self.calc_checksum(header[2:] + data)
        frame = header + data + bytes([checksum])
        
        self.serial.write(frame)
        return True
    
    def parse_frame(self, buffer):
        """解析帧，返回 (cmd, data, remaining_buffer) 或 None"""
        # 查找帧头
        idx = buffer.find(FRAME_HEADER)
        if idx < 0:
            return None, buffer[-1:] if buffer else b''
        
        buffer = buffer[idx:]
        
        # 检查最小帧长度
        if len(buffer) < 6:  # 帧头2 + 命令1 + 长度2 + 校验1
            return None, buffer
        
        cmd = buffer[2]
        length = struct.unpack('<H', buffer[3:5])[0]
        
        # 检查完整帧
        frame_len = 5 + length + 1
        if len(buffer) < frame_len:
            return None, buffer
        
        data = buffer[5:5+length]
        checksum = buffer[5+length]
        
        # 验证校验和
        calc_checksum = self.calc_checksum(buffer[2:5+length])
        if checksum != calc_checksum:
            print(f"校验和错误: 期望 0x{calc_checksum:02X}, 收到 0x{checksum:02X}")
            return None, buffer[2:]
        
        return (cmd, data), buffer[frame_len:]
    
    def rx_loop(self):
        """接收循环"""
        buffer = bytearray()
        
        while self.running:
            try:
                data = self.serial.read(1024)
                if data:
                    buffer.extend(data)
                    
                    while True:
                        result, buffer = self.parse_frame(bytes(buffer))
                        buffer = bytearray(buffer)
                        
                        if result is None:
                            break
                        
                        cmd, frame_data = result
                        self.handle_frame(cmd, frame_data)
                        
            except Exception as e:
                if self.running:
                    print(f"接收错误: {e}")
                break
    
    def handle_frame(self, cmd, data):
        """处理接收到的帧"""
        if cmd == CMD_AUDIO_DATA:
            self.audio_data.extend(data)
            print(f"\r接收音频数据: {len(self.audio_data)} 字节", end='', flush=True)
        elif cmd == CMD_ACK:
            if len(data) > 0:
                print(f"\n收到应答: 命令 0x{data[0]:02X}")
        else:
            print(f"\n收到未知命令: 0x{cmd:02X}")
    
    def handshake(self):
        """握手"""
        print("发送握手...")
        self.send_frame(CMD_HANDSHAKE)
        time.sleep(0.5)
    
    def start_record(self, output_file, duration=10):
        """开始录音"""
        self.audio_data = bytearray()
        self.running = True
        
        # 启动接收线程
        self.rx_thread = threading.Thread(target=self.rx_loop)
        self.rx_thread.start()
        
        # 发送开始录音命令
        print(f"开始录音, 时长: {duration} 秒...")
        self.send_frame(CMD_START_RECORD)
        
        try:
            time.sleep(duration)
        except KeyboardInterrupt:
            print("\n录音被中断")
        
        # 停止录音
        print("\n停止录音...")
        self.send_frame(CMD_STOP_RECORD)
        time.sleep(0.5)
        
        self.running = False
        self.rx_thread.join()
        
        # 保存为 WAV 文件
        if self.audio_data:
            self.save_wav(output_file)
        else:
            print("没有接收到音频数据")
    
    def save_wav(self, filename):
        """保存 WAV 文件"""
        with wave.open(filename, 'wb') as wf:
            wf.setnchannels(CHANNELS)
            wf.setsampwidth(BITS_PER_SAMPLE // 8)
            wf.setframerate(SAMPLE_RATE)
            wf.writeframes(bytes(self.audio_data))
        
        print(f"已保存: {filename}")
        print(f"  采样率: {SAMPLE_RATE} Hz")
        print(f"  位宽: {BITS_PER_SAMPLE} bit")
        print(f"  声道: {CHANNELS}")
        print(f"  大小: {len(self.audio_data)} 字节")
        print(f"  时长: {len(self.audio_data) / (SAMPLE_RATE * CHANNELS * BITS_PER_SAMPLE // 8):.2f} 秒")
    
    def load_audio_file(self, filename):
        """加载音频文件（支持 WAV 和 MP3 格式）
        
        返回：(frames, sample_rate, sample_width, channels, audio_format) 或 None
        """
        filepath = Path(filename)
        if not filepath.exists():
            print(f"文件不存在: {filename}")
            return None
        
        ext = filepath.suffix.lower()
        
        if ext == '.wav':
            # 加载 WAV 文件 - 发送 PCM 数据
            with wave.open(filename, 'rb') as wf:
                channels = wf.getnchannels()
                sample_width = wf.getsampwidth()
                framerate = wf.getframerate()
                frames = wf.readframes(wf.getnframes())
            return frames, framerate, sample_width, channels, AUDIO_FORMAT_PCM
        
        elif ext == '.mp3':
            # 加载 MP3 文件 - 直接发送原始 MP3 数据（ESP32端解码）
            print(f"MP3 文件将由 ESP32 硬件解码")
            with open(filename, 'rb') as f:
                frames = f.read()
            # 返回 MP3 原始数据，采样率/声道由 ESP32 解码后确定
            return frames, 0, 0, 0, AUDIO_FORMAT_MP3
        
        else:
            print(f"不支持的音频格式: {ext}")
            print("支持的格式: .wav, .mp3")
            return None
    
    def play_audio(self, filename):
        """发送音频文件播放（支持 WAV 和 MP3 格式）"""
        # 加载音频文件
        result = self.load_audio_file(filename)
        if result is None:
            return
        
        frames, framerate, sample_width, channels, audio_format = result
        
        print(f"播放文件: {filename}")
        if audio_format == AUDIO_FORMAT_MP3:
            print(f"  格式: MP3 (硬件解码)")
            print(f"  大小: {len(frames)} 字节")
        else:
            print(f"  格式: PCM")
            print(f"  采样率: {framerate} Hz")
            print(f"  位宽: {sample_width * 8} bit")
            print(f"  声道: {channels}")
            print(f"  大小: {len(frames)} 字节")
            print(f"  时长: {len(frames) / (framerate * channels * sample_width):.2f} 秒")
        
        # 启动接收线程
        self.running = True
        self.rx_thread = threading.Thread(target=self.rx_loop)
        self.rx_thread.start()
        
        # 发送设置格式命令 (新增)
        print(f"设置音频格式: {'MP3' if audio_format == AUDIO_FORMAT_MP3 else 'PCM'}")
        self.send_frame(CMD_SET_FORMAT, bytes([audio_format]))
        time.sleep(0.2)
        
        # 发送开始播放命令
        self.send_frame(CMD_START_PLAY)
        time.sleep(0.5)
        
        # 分包发送音频数据
        # MP3 数据使用更大的包大小以提高效率
        chunk_size = 1024 if audio_format == AUDIO_FORMAT_MP3 else 512
        total_chunks = (len(frames) + chunk_size - 1) // chunk_size
        
        print(f"发送音频数据...")
        for i in range(0, len(frames), chunk_size):
            chunk = frames[i:i+chunk_size]
            self.send_frame(CMD_AUDIO_DATA, chunk)
            print(f"\r进度: {i // chunk_size + 1}/{total_chunks}", end='', flush=True)
            # MP3 数据需要更快的发送速率以保证输入缓冲区
            time.sleep(0.01 if audio_format == AUDIO_FORMAT_MP3 else 0.02)
        
        print("\n发送完成")
        
        # 停止播放
        time.sleep(0.5)
        self.send_frame(CMD_STOP_PLAY)
        
        self.running = False
        self.rx_thread.join()
    
    # 保留 play_wav 作为别名以保持向后兼容
    def play_wav(self, filename):
        """发送 WAV 文件播放（已弃用，请使用 play_audio）"""
        self.play_audio(filename)

    def listen_record(self, output_file):
        """监听模式：等待按键开始/停止录音"""
        self.audio_data = bytearray()
        self.running = True
        
        # 启动接收线程
        self.rx_thread = threading.Thread(target=self.rx_loop)
        self.rx_thread.start()
        
        print("监听模式已启动")
        print("按 ESP32 上的 KEY0 开始录音")
        print("再按 KEY0 停止录音")
        print("按 Ctrl+C 退出监听模式")
        print("="*40)
        
        try:
            while self.running:
                time.sleep(0.1)
        except KeyboardInterrupt:
            print("\n用户中断")
        
        self.running = False
        self.rx_thread.join()
        
        # 保存为 WAV 文件
        if self.audio_data:
            self.save_wav(output_file)
        else:
            print("没有接收到音频数据")


def main():
    parser = argparse.ArgumentParser(description='ESP32-S3 音频串口传输工具')
    parser.add_argument('port', help='串口名称 (如 COM3 或 /dev/ttyUSB0)')
    parser.add_argument('--baud', type=int, default=230400, help='波特率 (默认: 230400)')
    
    subparsers = parser.add_subparsers(dest='command', help='命令')
    
    # 录音命令
    record_parser = subparsers.add_parser('record', help='录音')
    record_parser.add_argument('-o', '--output', default='recording.wav', help='输出文件 (默认: recording.wav)')
    record_parser.add_argument('-d', '--duration', type=int, default=10, help='录音时长(秒) (默认: 10)')
    
    # 播放命令
    play_parser = subparsers.add_parser('play', help='播放音频文件')
    play_parser.add_argument('file', help='音频文件 (支持 WAV/MP3 格式)')
    
    # 握手命令
    subparsers.add_parser('handshake', help='握手测试')
    
    # 监听模式
    listen_parser = subparsers.add_parser('listen', help='监听模式（等待按键录音）')
    listen_parser.add_argument('-o', '--output', default='recording.wav', help='输出文件 (默认: recording.wav)')
    
    args = parser.parse_args()
    
    if not args.command:
        parser.print_help()
        return
    
    tool = AudioSerialTool(args.port, args.baud)
    
    if not tool.connect():
        return
    
    try:
        if args.command == 'record':
            tool.start_record(args.output, args.duration)
        elif args.command == 'play':
            tool.play_audio(args.file)
        elif args.command == 'handshake':
            tool.running = True
            tool.rx_thread = threading.Thread(target=tool.rx_loop)
            tool.rx_thread.start()
            tool.handshake()
            time.sleep(1)
            tool.running = False
            tool.rx_thread.join()
        elif args.command == 'listen':
            tool.listen_record(args.output)
    except KeyboardInterrupt:
        print("\n操作被中断")
    finally:
        tool.disconnect()


if __name__ == '__main__':
    main()
