/**
 ****************************************************************************************************
 * @file        uart_audio.c
 * @author      Audio Serial Transfer
 * @version     V1.0
 * @date        2024-12-20
 * @brief       串口音频传输模块 - 支持录音和播放
 ****************************************************************************************************
 */

#include "uart_audio.h"
#include "i2s.h"
#include "es8388.h"
#include "mp3_decoder.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "xl9555.h"
#include <string.h>

static const char *TAG = "UART_AUDIO";

/* 全局变量 */
static uart_port_t g_uart_num = UART_NUM_0;
static audio_mode_t g_mode = MODE_IDLE;
static TaskHandle_t g_rx_task_handle = NULL;
static TaskHandle_t g_record_task_handle = NULL;
static TaskHandle_t g_play_task_handle = NULL;
static volatile bool g_running = false;
static audio_format_t g_audio_format = AUDIO_FORMAT_PCM;  /* 当前音频格式 */

/* 音频缓冲区 */
static uint8_t *g_audio_buf = NULL;
static QueueHandle_t g_play_queue = NULL;

/* 帧解析状态 */
typedef enum {
    PARSE_HEADER_0,
    PARSE_HEADER_1,
    PARSE_CMD,
    PARSE_LEN_L,
    PARSE_LEN_H,
    PARSE_DATA,
    PARSE_CHECKSUM,
} parse_state_t;

/**
 * @brief       计算校验和
 */
static uint8_t calc_checksum(const uint8_t *data, uint16_t len)
{
    uint8_t sum = 0;
    for (uint16_t i = 0; i < len; i++) {
        sum ^= data[i];
    }
    return sum;
}

/**
 * @brief       发送音频帧
 */
int uart_audio_send_frame(uint8_t cmd, const uint8_t *data, uint16_t len)
{
    uint8_t header[5];
    header[0] = FRAME_HEADER_0;
    header[1] = FRAME_HEADER_1;
    header[2] = cmd;
    header[3] = len & 0xFF;
    header[4] = (len >> 8) & 0xFF;
    
    /* 计算校验和 */
    uint8_t checksum = calc_checksum(header + 2, 3);
    if (data && len > 0) {
        checksum ^= calc_checksum(data, len);
    }
    
    /* 发送帧头 */
    uart_write_bytes(g_uart_num, (const char *)header, 5);
    
    /* 发送数据 */
    if (data && len > 0) {
        uart_write_bytes(g_uart_num, (const char *)data, len);
    }
    
    /* 发送校验和 */
    uart_write_bytes(g_uart_num, (const char *)&checksum, 1);
    
    return 5 + len + 1;
}

/**
 * @brief       处理接收到的帧
 */
static void process_frame(uint8_t cmd, const uint8_t *data, uint16_t len)
{
    switch (cmd) {
        case CMD_START_RECORD:
            ESP_LOGI(TAG, "收到开始录音命令");
            if (g_mode == MODE_IDLE) {
                g_mode = MODE_RECORDING;
                /* 配置ES8388为录音模式 */
                es8388_adda_cfg(0, 1);      /* ADC开启 */
                es8388_input_cfg(0);        /* 输入通道1 */
                es8388_mic_gain(8);         /* MIC增益 */
                i2s_trx_start();
            }
            /* 发送应答 */
            uart_audio_send_frame(CMD_ACK, (uint8_t *)&cmd, 1);
            break;
            
        case CMD_STOP_RECORD:
            ESP_LOGI(TAG, "收到停止录音命令");
            if (g_mode == MODE_RECORDING) {
                g_mode = MODE_IDLE;
                i2s_trx_stop();
            }
            uart_audio_send_frame(CMD_ACK, (uint8_t *)&cmd, 1);
            break;
            
        case CMD_START_PLAY:
            ESP_LOGI(TAG, "收到开始播放命令, 格式: %s", 
                     g_audio_format == AUDIO_FORMAT_MP3 ? "MP3" : "PCM");
            if (g_mode == MODE_IDLE) {
                g_mode = MODE_PLAYING;
                /* 开启喇叭功放 (低电平有效) */
                xl9555_pin_write(SPK_EN_IO, 0);
                /* 配置ES8388为播放模式 */
                es8388_adda_cfg(1, 0);      /* DAC开启 */
                es8388_output_cfg(1, 1);    /* 输出通道开启 */
                es8388_sai_cfg(0, 3);       /* 设置I2S模式: 标准I2S, 16bit */
                es8388_hpvol_set(30);       /* 设置耳机音量 */
                es8388_spkvol_set(30);      /* 设置喇叭音量 */
                i2s_trx_start();
                
                /* 如果是 MP3 格式，初始化解码器 */
                if (g_audio_format == AUDIO_FORMAT_MP3) {
                    mp3_decoder_init();
                }
            }
            uart_audio_send_frame(CMD_ACK, (uint8_t *)&cmd, 1);
            break;
            
        case CMD_STOP_PLAY:
            ESP_LOGI(TAG, "收到停止播放命令");
            if (g_mode == MODE_PLAYING) {
                g_mode = MODE_IDLE;
                i2s_trx_stop();
                /* 关闭喇叭功放 (低电平有效) */
                xl9555_pin_write(SPK_EN_IO, 1);
                
                /* 释放 MP3 解码器 */
                if (mp3_decoder_is_initialized()) {
                    mp3_decoder_deinit();
                }
            }
            /* 重置为 PCM 格式 */
            g_audio_format = AUDIO_FORMAT_PCM;
            uart_audio_send_frame(CMD_ACK, (uint8_t *)&cmd, 1);
            break;
            
        case CMD_AUDIO_DATA:
            /* 播放模式下接收音频数据 */
            if (g_mode == MODE_PLAYING && len > 0 && g_audio_buf) {
                if (g_audio_format == AUDIO_FORMAT_MP3) {
                    /* MP3 格式：先解码再播放 */
                    mp3_decoder_feed(data, len);
                    
                    /* 尝试获取解码后的 PCM 数据 */
                    int sample_rate = 0, channels = 0;
                    int samples = mp3_decoder_get_pcm((int16_t *)g_audio_buf, 
                                                       FRAME_MAX_DATA_SIZE / sizeof(int16_t), 
                                                       &sample_rate, &channels);
                    
                    if (samples > 0) {
                        /* 根据解码的声道数计算输出 */
                        size_t pcm_bytes = samples * channels * sizeof(int16_t);
                        
                        if (channels == 1) {
                            /* 单声道转立体声 */
                            int16_t *mono = (int16_t *)g_audio_buf;
                            static int16_t stereo_buf[4096];
                            for (int i = samples - 1; i >= 0; i--) {
                                stereo_buf[i * 2] = mono[i];
                                stereo_buf[i * 2 + 1] = mono[i];
                            }
                            i2s_tx_write((uint8_t *)stereo_buf, samples * 4);
                        } else {
                            /* 立体声直接输出 */
                            i2s_tx_write(g_audio_buf, pcm_bytes);
                        }
                        
                        /* 调试：每50帧打印一次 */
                        static uint32_t mp3_frame_count = 0;
                        mp3_frame_count++;
                        if (mp3_frame_count % 50 == 1) {
                            ESP_LOGI(TAG, "MP3帧 #%lu: %d采样, %dHz, %d声道", 
                                     mp3_frame_count, samples, sample_rate, channels);
                        }
                    }
                } else {
                    /* PCM 格式：直接播放 */
                    /* 输入：单声道16bit PCM，每个采样2字节 */
                    /* 输出：立体声16bit PCM，每个采样4字节（左右声道相同） */
                    int16_t *mono_data = (int16_t *)data;
                    int16_t *stereo_data = (int16_t *)g_audio_buf;
                    uint16_t samples = len / 2;  /* 单声道采样数 */
                    
                    for (uint16_t i = 0; i < samples; i++) {
                        stereo_data[i * 2] = mono_data[i];      /* 左声道 */
                        stereo_data[i * 2 + 1] = mono_data[i];  /* 右声道 */
                    }
                    
                    /* 写入I2S，立体声数据长度是单声道的2倍 */
                    size_t written = i2s_tx_write(g_audio_buf, len * 2);
                    
                    /* 调试：每100帧打印一次 */
                    static uint32_t frame_count = 0;
                    frame_count++;
                    if (frame_count % 100 == 1) {
                        ESP_LOGI(TAG, "PCM帧 #%lu: 输入%d字节, I2S写入%d字节", 
                                 frame_count, len, (int)written);
                    }
                }
            }
            break;
        
        case CMD_SET_FORMAT:
            /* 设置音频格式 */
            if (len >= 1) {
                g_audio_format = (audio_format_t)data[0];
                ESP_LOGI(TAG, "设置音频格式: %s", 
                         g_audio_format == AUDIO_FORMAT_MP3 ? "MP3" : "PCM");
            }
            uart_audio_send_frame(CMD_ACK, (uint8_t *)&cmd, 1);
            break;
            
        case CMD_HANDSHAKE:
            ESP_LOGI(TAG, "收到握手命令");
            {
                uint8_t status = (uint8_t)g_mode;
                uart_audio_send_frame(CMD_ACK, &status, 1);
            }
            break;
            
        default:
            ESP_LOGW(TAG, "未知命令: 0x%02X", cmd);
            break;
    }
}

/**
 * @brief       串口接收任务
 */
static void uart_rx_task(void *arg)
{
    uint8_t byte;
    parse_state_t state = PARSE_HEADER_0;
    uint8_t cmd = 0;
    uint16_t data_len = 0;
    uint16_t data_idx = 0;
    static uint8_t frame_data[FRAME_MAX_DATA_SIZE];
    uint8_t checksum_calc = 0;
    
    ESP_LOGI(TAG, "串口接收任务启动");
    
    while (g_running) {
        int len = uart_read_bytes(g_uart_num, &byte, 1, pdMS_TO_TICKS(10));
        if (len <= 0) continue;
        
        switch (state) {
            case PARSE_HEADER_0:
                if (byte == FRAME_HEADER_0) {
                    state = PARSE_HEADER_1;
                }
                break;
                
            case PARSE_HEADER_1:
                if (byte == FRAME_HEADER_1) {
                    state = PARSE_CMD;
                } else {
                    state = PARSE_HEADER_0;
                }
                break;
                
            case PARSE_CMD:
                cmd = byte;
                checksum_calc = byte;
                state = PARSE_LEN_L;
                break;
                
            case PARSE_LEN_L:
                data_len = byte;
                checksum_calc ^= byte;
                state = PARSE_LEN_H;
                break;
                
            case PARSE_LEN_H:
                data_len |= (byte << 8);
                checksum_calc ^= byte;
                data_idx = 0;
                if (data_len > 0 && data_len <= FRAME_MAX_DATA_SIZE) {
                    state = PARSE_DATA;
                } else if (data_len == 0) {
                    state = PARSE_CHECKSUM;
                } else {
                    ESP_LOGW(TAG, "数据长度无效: %d", data_len);
                    state = PARSE_HEADER_0;
                }
                break;
                
            case PARSE_DATA:
                frame_data[data_idx++] = byte;
                checksum_calc ^= byte;
                if (data_idx >= data_len) {
                    state = PARSE_CHECKSUM;
                }
                break;
                
            case PARSE_CHECKSUM:
                if (byte == checksum_calc) {
                    process_frame(cmd, frame_data, data_len);
                } else {
                    ESP_LOGW(TAG, "校验和错误: 期望0x%02X, 收到0x%02X", checksum_calc, byte);
                }
                state = PARSE_HEADER_0;
                break;
        }
    }
    
    ESP_LOGI(TAG, "串口接收任务退出");
    vTaskDelete(NULL);
}

/**
 * @brief       录音任务
 */
static void record_task(void *arg)
{
    uint8_t *buf = heap_caps_malloc(AUDIO_FRAME_SIZE, MALLOC_CAP_DMA);
    if (!buf) {
        ESP_LOGE(TAG, "录音缓冲区分配失败");
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "录音任务启动");
    
    while (g_running) {
        if (g_mode == MODE_RECORDING) {
            /* 从I2S读取音频数据 */
            size_t bytes_read = i2s_rx_read(buf, AUDIO_FRAME_SIZE);
            if (bytes_read > 0) {
                /* 通过串口发送 */
                uart_audio_send_frame(CMD_AUDIO_DATA, buf, bytes_read);
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    
    free(buf);
    ESP_LOGI(TAG, "录音任务退出");
    vTaskDelete(NULL);
}

/**
 * @brief       初始化串口音频模块
 */
esp_err_t uart_audio_init(uart_port_t uart_num, int tx_pin, int rx_pin)
{
    esp_err_t ret = ESP_OK;
    
    g_uart_num = uart_num;
    
    /* 串口配置 */
    uart_config_t uart_config = {
        .baud_rate = UART_AUDIO_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    
    ret = uart_param_config(uart_num, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "串口参数配置失败");
        return ret;
    }
    
    /* 设置引脚 */
    if (tx_pin >= 0 || rx_pin >= 0) {
        ret = uart_set_pin(uart_num, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "串口引脚配置失败");
            return ret;
        }
    }
    
    /* 安装驱动 */
    ret = uart_driver_install(uart_num, UART_BUF_SIZE * 2, UART_BUF_SIZE * 2, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "串口驱动安装失败");
        return ret;
    }
    
    /* 分配音频缓冲区（2倍大小，用于单声道转立体声） */
    g_audio_buf = heap_caps_malloc(FRAME_MAX_DATA_SIZE * 2, MALLOC_CAP_DMA);
    if (!g_audio_buf) {
        ESP_LOGE(TAG, "音频缓冲区分配失败");
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "串口音频模块初始化完成, UART%d, 波特率: %d", uart_num, UART_AUDIO_BAUD_RATE);
    
    return ESP_OK;
}

/**
 * @brief       启动音频处理任务
 */
esp_err_t uart_audio_start(void)
{
    if (g_running) {
        return ESP_OK;
    }
    
    g_running = true;
    
    /* 创建串口接收任务 */
    xTaskCreatePinnedToCore(uart_rx_task, "uart_rx", 4096, NULL, 10, &g_rx_task_handle, 0);
    
    /* 创建录音任务 */
    xTaskCreatePinnedToCore(record_task, "record", 4096, NULL, 10, &g_record_task_handle, 1);
    
    ESP_LOGI(TAG, "音频处理任务启动");
    
    return ESP_OK;
}

/**
 * @brief       停止音频处理
 */
void uart_audio_stop(void)
{
    g_running = false;
    g_mode = MODE_IDLE;
    vTaskDelay(pdMS_TO_TICKS(100));
}

/**
 * @brief       获取当前工作模式
 */
audio_mode_t uart_audio_get_mode(void)
{
    return g_mode;
}

/**
 * @brief       手动开始录音
 */
esp_err_t uart_audio_start_record(void)
{
    if (g_mode != MODE_IDLE) {
        return ESP_ERR_INVALID_STATE;
    }
    
    g_mode = MODE_RECORDING;
    es8388_adda_cfg(0, 1);
    es8388_input_cfg(0);
    es8388_mic_gain(8);
    i2s_trx_start();
    
    ESP_LOGI(TAG, "开始录音");
    return ESP_OK;
}

/**
 * @brief       手动停止录音
 */
void uart_audio_stop_record(void)
{
    if (g_mode == MODE_RECORDING) {
        g_mode = MODE_IDLE;
        i2s_trx_stop();
        ESP_LOGI(TAG, "停止录音");
    }
}
