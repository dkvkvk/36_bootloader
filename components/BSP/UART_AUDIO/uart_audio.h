/**
 ****************************************************************************************************
 * @file        uart_audio.h
 * @author      Audio Serial Transfer
 * @version     V1.0
 * @date        2024-12-20
 * @brief       串口音频传输模块 - 支持录音和播放
 ****************************************************************************************************
 */

#ifndef __UART_AUDIO_H__
#define __UART_AUDIO_H__

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/uart.h"

/* 音频配置 */
#define AUDIO_SAMPLE_RATE       16000           /* 采样率: 16kHz */
#define AUDIO_BITS_PER_SAMPLE   16              /* 位宽: 16bit */
#define AUDIO_CHANNELS          1               /* 声道: 单声道 */
#define AUDIO_FRAME_SIZE        512             /* 每帧大小(字节) */

/* 串口配置 */
#define UART_AUDIO_BAUD_RATE    921600          /* 波特率 */
#define UART_BUF_SIZE           2048            /* 串口缓冲区大小 */

/* 协议帧定义 */
#define FRAME_HEADER_0          0xAA            /* 帧头第一字节 */
#define FRAME_HEADER_1          0x55            /* 帧头第二字节 */
#define FRAME_MAX_DATA_SIZE     2048            /* 最大数据长度 (增大以支持MP3帧) */

/* 音频格式定义 */
typedef enum {
    AUDIO_FORMAT_PCM = 0x00,        /* 原始 PCM 数据 */
    AUDIO_FORMAT_MP3 = 0x01,        /* MP3 压缩数据 */
} audio_format_t;

/* 命令定义 */
typedef enum {
    CMD_START_RECORD    = 0x01,     /* 开始录音 */
    CMD_STOP_RECORD     = 0x02,     /* 停止录音 */
    CMD_AUDIO_DATA      = 0x03,     /* 音频数据包 */
    CMD_START_PLAY      = 0x04,     /* 开始播放 */
    CMD_STOP_PLAY       = 0x05,     /* 停止播放 */
    CMD_HANDSHAKE       = 0x06,     /* 握手/状态查询 */
    CMD_ACK             = 0x07,     /* 应答 */
    CMD_SET_FORMAT      = 0x08,     /* 设置音频格式 (PCM/MP3) */
} audio_cmd_t;

/* 工作模式 */
typedef enum {
    MODE_IDLE = 0,                  /* 空闲模式 */
    MODE_RECORDING,                 /* 录音模式 */
    MODE_PLAYING,                   /* 播放模式 */
} audio_mode_t;

/* 协议帧结构 */
typedef struct {
    uint8_t header[2];              /* 帧头: 0xAA 0x55 */
    uint8_t cmd;                    /* 命令 */
    uint16_t length;                /* 数据长度 */
    uint8_t data[FRAME_MAX_DATA_SIZE]; /* 数据 */
    uint8_t checksum;               /* 校验和(XOR) */
} __attribute__((packed)) audio_frame_t;

/* 函数声明 */

/**
 * @brief       初始化串口音频模块
 * @param       uart_num: 使用的串口号 (UART_NUM_0, UART_NUM_1, UART_NUM_2)
 * @param       tx_pin: TX引脚 (-1表示使用默认引脚)
 * @param       rx_pin: RX引脚 (-1表示使用默认引脚)
 * @retval      ESP_OK: 成功; 其他: 失败
 */
esp_err_t uart_audio_init(uart_port_t uart_num, int tx_pin, int rx_pin);

/**
 * @brief       启动音频处理任务
 * @retval      ESP_OK: 成功; 其他: 失败
 */
esp_err_t uart_audio_start(void);

/**
 * @brief       停止音频处理
 * @retval      无
 */
void uart_audio_stop(void);

/**
 * @brief       获取当前工作模式
 * @retval      当前模式
 */
audio_mode_t uart_audio_get_mode(void);

/**
 * @brief       手动开始录音
 * @retval      ESP_OK: 成功
 */
esp_err_t uart_audio_start_record(void);

/**
 * @brief       手动停止录音
 * @retval      无
 */
void uart_audio_stop_record(void);

/**
 * @brief       发送音频帧
 * @param       cmd: 命令
 * @param       data: 数据
 * @param       len: 数据长度
 * @retval      发送的字节数
 */
int uart_audio_send_frame(uint8_t cmd, const uint8_t *data, uint16_t len);

#endif /* __UART_AUDIO_H__ */
