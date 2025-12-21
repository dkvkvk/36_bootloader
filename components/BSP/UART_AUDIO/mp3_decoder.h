/**
 ****************************************************************************************************
 * @file        mp3_decoder.h
 * @author      Audio Serial Transfer
 * @version     V1.0
 * @date        2024-12-21
 * @brief       MP3 解码器模块 - 使用 esp_audio_codec
 ****************************************************************************************************
 */

#ifndef __MP3_DECODER_H__
#define __MP3_DECODER_H__

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/* MP3 解码器配置 */
#define MP3_INPUT_BUFFER_SIZE       4096    /* MP3 输入缓冲区大小 */
#define MP3_OUTPUT_BUFFER_SIZE      4608    /* PCM 输出缓冲区大小 (1152 samples * 2 channels * 2 bytes) */

/**
 * @brief       初始化 MP3 解码器
 * @retval      ESP_OK: 成功; 其他: 失败
 */
esp_err_t mp3_decoder_init(void);

/**
 * @brief       反初始化 MP3 解码器
 */
void mp3_decoder_deinit(void);

/**
 * @brief       喂入 MP3 数据到解码器
 * @param       data: MP3 数据
 * @param       len: 数据长度
 * @retval      实际消耗的字节数
 */
int mp3_decoder_feed(const uint8_t *data, size_t len);

/**
 * @brief       从解码器获取 PCM 数据
 * @param       pcm_out: PCM 输出缓冲区
 * @param       max_samples: 最大采样数
 * @param       sample_rate: 输出采样率
 * @param       channels: 输出声道数
 * @retval      实际输出的采样数 (每声道), 0 表示数据不足
 */
int mp3_decoder_get_pcm(int16_t *pcm_out, size_t max_samples, 
                        int *sample_rate, int *channels);

/**
 * @brief       重置解码器状态
 */
void mp3_decoder_reset(void);

/**
 * @brief       检查解码器是否已初始化
 * @retval      true: 已初始化; false: 未初始化
 */
bool mp3_decoder_is_initialized(void);

#endif /* __MP3_DECODER_H__ */
