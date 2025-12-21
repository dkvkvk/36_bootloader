/**
 ****************************************************************************************************
 * @file        mp3_decoder.c
 * @author      Audio Serial Transfer
 * @version     V1.0
 * @date        2024-12-21
 * @brief       MP3 解码器模块 - 使用 esp_audio_codec 的 simple decoder
 ****************************************************************************************************
 */

#include "mp3_decoder.h"
#include "esp_audio_simple_dec.h"
#include "esp_audio_simple_dec_default.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>

static const char *TAG = "MP3_DEC";

/* 解码器句柄 */
static esp_audio_simple_dec_handle_t s_decoder = NULL;

/* 输入缓冲区 */
static uint8_t *s_input_buf = NULL;
static size_t s_input_buf_len = 0;
static size_t s_input_buf_pos = 0;

/* 解码器是否已初始化 */
static bool s_initialized = false;

/**
 * @brief       初始化 MP3 解码器
 */
esp_err_t mp3_decoder_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "解码器已初始化");
        return ESP_OK;
    }
    
    /* 注册默认解码器 */
    esp_audio_simple_dec_register_default();
    
    /* 分配输入缓冲区 */
    s_input_buf = heap_caps_malloc(MP3_INPUT_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_input_buf) {
        s_input_buf = heap_caps_malloc(MP3_INPUT_BUFFER_SIZE, MALLOC_CAP_8BIT);
    }
    if (!s_input_buf) {
        ESP_LOGE(TAG, "输入缓冲区分配失败");
        return ESP_ERR_NO_MEM;
    }
    s_input_buf_len = 0;
    s_input_buf_pos = 0;
    
    /* 配置 Simple Decoder */
    esp_audio_simple_dec_cfg_t cfg = {
        .dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3,
    };
    
    /* 创建解码器 */
    esp_audio_err_t ret = esp_audio_simple_dec_open(&cfg, &s_decoder);
    if (ret != ESP_AUDIO_ERR_OK) {
        ESP_LOGE(TAG, "创建解码器失败: %d", ret);
        free(s_input_buf);
        s_input_buf = NULL;
        return ESP_FAIL;
    }
    
    s_initialized = true;
    ESP_LOGI(TAG, "MP3 解码器初始化完成");
    
    return ESP_OK;
}

/**
 * @brief       反初始化 MP3 解码器
 */
void mp3_decoder_deinit(void)
{
    if (!s_initialized) {
        return;
    }
    
    if (s_decoder) {
        esp_audio_simple_dec_close(s_decoder);
        s_decoder = NULL;
    }
    
    if (s_input_buf) {
        free(s_input_buf);
        s_input_buf = NULL;
    }
    
    s_input_buf_len = 0;
    s_input_buf_pos = 0;
    s_initialized = false;
    
    ESP_LOGI(TAG, "MP3 解码器已释放");
}

/**
 * @brief       喂入 MP3 数据到解码器
 */
int mp3_decoder_feed(const uint8_t *data, size_t len)
{
    if (!s_initialized || !data || len == 0) {
        return 0;
    }
    
    /* 移动已消耗的数据 */
    if (s_input_buf_pos > 0) {
        size_t remaining = s_input_buf_len - s_input_buf_pos;
        if (remaining > 0) {
            memmove(s_input_buf, s_input_buf + s_input_buf_pos, remaining);
        }
        s_input_buf_len = remaining;
        s_input_buf_pos = 0;
    }
    
    /* 计算可接收的数据量 */
    size_t space = MP3_INPUT_BUFFER_SIZE - s_input_buf_len;
    size_t to_copy = (len < space) ? len : space;
    
    if (to_copy > 0) {
        memcpy(s_input_buf + s_input_buf_len, data, to_copy);
        s_input_buf_len += to_copy;
    }
    
    return (int)to_copy;
}

/**
 * @brief       从解码器获取 PCM 数据
 */
int mp3_decoder_get_pcm(int16_t *pcm_out, size_t max_samples, 
                        int *sample_rate, int *channels)
{
    if (!s_initialized || !s_decoder || !pcm_out) {
        return 0;
    }
    
    /* 检查是否有足够数据 */
    size_t available = s_input_buf_len - s_input_buf_pos;
    if (available < 128) {
        /* 数据不足，等待更多数据 */
        return 0;
    }
    
    /* 准备解码输入 */
    esp_audio_simple_dec_raw_t raw_in = {
        .buffer = s_input_buf + s_input_buf_pos,
        .len = available,
        .eos = false,
    };
    
    /* 准备解码输出 */
    esp_audio_simple_dec_out_t dec_out = {
        .buffer = (uint8_t *)pcm_out,
        .len = max_samples * sizeof(int16_t) * 2,  /* 最大立体声输出 */
    };
    
    /* 解码 */
    esp_audio_err_t ret = esp_audio_simple_dec_process(s_decoder, &raw_in, &dec_out);
    if (ret != ESP_AUDIO_ERR_OK && ret != ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
        ESP_LOGW(TAG, "解码错误: %d", ret);
        return 0;
    }
    
    /* 更新消耗位置 */
    s_input_buf_pos += raw_in.consumed;
    
    /* 获取音频信息 */
    if (dec_out.decoded_size > 0) {
        esp_audio_simple_dec_info_t info;
        if (esp_audio_simple_dec_get_info(s_decoder, &info) == ESP_AUDIO_ERR_OK) {
            if (sample_rate) *sample_rate = info.sample_rate;
            if (channels) *channels = info.channel;
        } else {
            if (sample_rate) *sample_rate = 44100;
            if (channels) *channels = 2;
        }
        
        /* 计算输出采样数 (每声道) */
        int ch = (channels && *channels > 0) ? *channels : 2;
        return dec_out.decoded_size / sizeof(int16_t) / ch;
    }
    
    return 0;
}

/**
 * @brief       重置解码器状态
 */
void mp3_decoder_reset(void)
{
    if (!s_initialized) {
        return;
    }
    
    /* 清空输入缓冲区 */
    s_input_buf_len = 0;
    s_input_buf_pos = 0;
    
    /* 重新打开解码器 */
    if (s_decoder) {
        esp_audio_simple_dec_close(s_decoder);
        s_decoder = NULL;
    }
    
    esp_audio_simple_dec_cfg_t cfg = {
        .dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3,
    };
    esp_audio_simple_dec_open(&cfg, &s_decoder);
    
    ESP_LOGI(TAG, "解码器已重置");
}

/**
 * @brief       检查解码器是否已初始化
 */
bool mp3_decoder_is_initialized(void)
{
    return s_initialized;
}
