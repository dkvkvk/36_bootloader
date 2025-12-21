/**
 ****************************************************************************************************
 * @file        mp3_decoder.c
 * @author      Audio Serial Transfer
 * @version     V3.1
 * @date        2024-12-21
 * @brief       MP3 解码器模块 - 使用 esp_audio_dec API（通用解码器接口）
 ****************************************************************************************************
 */

#include "mp3_decoder.h"
#include "esp_audio_dec.h"
#include "esp_audio_dec_reg.h"
#include "esp_audio_dec_default.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>

static const char *TAG = "MP3_DEC";

/* 内部输出缓冲区大小 - 必须足够大以容纳一个完整的解码帧 */
/* MP3 帧: 1152 samples * 2 channels * 2 bytes = 4608 bytes, 再留一些余量 */
#define MP3_DECODE_OUTPUT_SIZE      8192

/* 解码器句柄 */
static esp_audio_dec_handle_t s_decoder = NULL;

/* 输入缓冲区 */
static uint8_t *s_input_buf = NULL;
static size_t s_input_buf_len = 0;
static size_t s_input_buf_pos = 0;

/* 内部输出缓冲区 */
static uint8_t *s_output_buf = NULL;
static size_t s_output_buf_size = MP3_DECODE_OUTPUT_SIZE;

/* 解码器是否已初始化 */
static bool s_initialized = false;

/* ID3 标签跳过状态 */
static bool s_id3_checked = false;
static size_t s_id3_skip_bytes = 0;
static bool s_sync_found = false;

/* 错误恢复计数器 */
static int s_error_count = 0;

/* 缓存的音频信息 */
static int s_sample_rate = 44100;
static int s_channels = 2;

/**
 * @brief       检查并跳过 ID3v2 标签
 */
static size_t check_id3v2_tag(const uint8_t *data, size_t len)
{
    if (len < 10) {
        return 0;
    }
    
    if (data[0] == 'I' && data[1] == 'D' && data[2] == '3') {
        size_t tag_size = ((data[6] & 0x7F) << 21) |
                          ((data[7] & 0x7F) << 14) |
                          ((data[8] & 0x7F) << 7) |
                          (data[9] & 0x7F);
        tag_size += 10;
        ESP_LOGI(TAG, "检测到 ID3v2 标签, 大小: %d 字节", (int)tag_size);
        return tag_size;
    }
    
    return 0;
}

/**
 * @brief       查找 MP3 同步字
 */
static int find_mp3_sync(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len - 1; i++) {
        if (data[i] == 0xFF && (data[i+1] & 0xE0) == 0xE0) {
            return (int)i;
        }
    }
    return -1;
}

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
    esp_audio_err_t ret = esp_audio_dec_register_default();
    if (ret != ESP_AUDIO_ERR_OK) {
        ESP_LOGW(TAG, "注册默认解码器返回: %d", ret);
    }
    
    /* 分配输入缓冲区 */
    s_input_buf = heap_caps_malloc(MP3_INPUT_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_input_buf) {
        s_input_buf = heap_caps_malloc(MP3_INPUT_BUFFER_SIZE, MALLOC_CAP_8BIT);
    }
    if (!s_input_buf) {
        ESP_LOGE(TAG, "输入缓冲区分配失败");
        return ESP_ERR_NO_MEM;
    }
    
    /* 分配内部输出缓冲区 */
    s_output_buf = heap_caps_malloc(s_output_buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_output_buf) {
        s_output_buf = heap_caps_malloc(s_output_buf_size, MALLOC_CAP_8BIT);
    }
    if (!s_output_buf) {
        ESP_LOGE(TAG, "输出缓冲区分配失败");
        free(s_input_buf);
        s_input_buf = NULL;
        return ESP_ERR_NO_MEM;
    }
    
    s_input_buf_len = 0;
    s_input_buf_pos = 0;
    
    /* 打开 MP3 解码器 */
    esp_audio_dec_cfg_t cfg = {
        .type = ESP_AUDIO_TYPE_MP3,
        .cfg = NULL,
        .cfg_sz = 0,
    };
    
    ret = esp_audio_dec_open(&cfg, &s_decoder);
    if (ret != ESP_AUDIO_ERR_OK) {
        ESP_LOGE(TAG, "打开 MP3 解码器失败: %d", ret);
        free(s_input_buf);
        free(s_output_buf);
        s_input_buf = NULL;
        s_output_buf = NULL;
        return ESP_FAIL;
    }
    
    s_id3_checked = false;
    s_id3_skip_bytes = 0;
    s_sync_found = false;
    s_error_count = 0;
    
    s_initialized = true;
    ESP_LOGI(TAG, "MP3 解码器初始化完成 (输出缓冲区: %d 字节)", (int)s_output_buf_size);
    
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
        esp_audio_dec_close(s_decoder);
        s_decoder = NULL;
    }
    
    if (s_input_buf) {
        free(s_input_buf);
        s_input_buf = NULL;
    }
    
    if (s_output_buf) {
        free(s_output_buf);
        s_output_buf = NULL;
    }
    
    s_input_buf_len = 0;
    s_input_buf_pos = 0;
    s_id3_checked = false;
    s_id3_skip_bytes = 0;
    s_sync_found = false;
    s_error_count = 0;
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
    
    const uint8_t *src = data;
    size_t src_len = len;
    
    /* 首次接收数据时检查 ID3 标签 */
    if (!s_id3_checked && s_input_buf_len == 0) {
        s_id3_skip_bytes = check_id3v2_tag(src, src_len);
        s_id3_checked = true;
    }
    
    /* 跳过 ID3 标签数据 */
    if (s_id3_skip_bytes > 0) {
        size_t skip = (src_len < s_id3_skip_bytes) ? src_len : s_id3_skip_bytes;
        src += skip;
        src_len -= skip;
        s_id3_skip_bytes -= skip;
        
        if (src_len == 0) {
            return (int)len;
        }
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
    size_t to_copy = (src_len < space) ? src_len : space;
    
    if (to_copy > 0) {
        memcpy(s_input_buf + s_input_buf_len, src, to_copy);
        s_input_buf_len += to_copy;
        
        /* 如果是首批有效数据，尝试查找 MP3 同步字 */
        if (!s_sync_found && s_input_buf_len >= 4) {
            ESP_LOGI(TAG, "缓冲区前8字节: %02X %02X %02X %02X %02X %02X %02X %02X",
                     s_input_buf[0], s_input_buf[1], s_input_buf[2], s_input_buf[3],
                     s_input_buf[4], s_input_buf[5], s_input_buf[6], s_input_buf[7]);
            
            int sync_pos = find_mp3_sync(s_input_buf, s_input_buf_len);
            if (sync_pos > 0) {
                ESP_LOGI(TAG, "找到 MP3 同步字位置: %d", sync_pos);
                memmove(s_input_buf, s_input_buf + sync_pos, s_input_buf_len - sync_pos);
                s_input_buf_len -= sync_pos;
            }
            if (sync_pos >= 0) {
                s_sync_found = true;
            }
        }
    }
    
    return (int)len;
}

/**
 * @brief       从解码器获取 PCM 数据
 */
int mp3_decoder_get_pcm(int16_t *pcm_out, size_t max_samples, 
                        int *sample_rate, int *channels)
{
    if (!s_initialized || !s_decoder || !pcm_out || !s_output_buf) {
        return 0;
    }
    
    /* 检查是否有足够数据 */
    size_t available = s_input_buf_len - s_input_buf_pos;
    if (available < 128) {
        return 0;
    }
    
    /* 准备解码输入 */
    esp_audio_dec_in_raw_t raw_in = {
        .buffer = s_input_buf + s_input_buf_pos,
        .len = available,
    };
    
    /* 使用内部大缓冲区作为输出 */
    esp_audio_dec_out_frame_t frame_out = {
        .buffer = s_output_buf,
        .len = s_output_buf_size,
        .decoded_size = 0,  /* 重要：初始化为0，避免残留值 */
    };
    
    /* 解码 */
    esp_audio_err_t ret = esp_audio_dec_process(s_decoder, &raw_in, &frame_out);
    
    /* 调试：每10次打印一次 */
    static uint32_t decode_call_count = 0;
    decode_call_count++;
    if (decode_call_count % 10 == 1) {
        ESP_LOGI(TAG, "解码 #%lu: ret=%d, consumed=%lu, decoded=%lu, pos=%lu, len=%lu", 
                 decode_call_count, ret, (unsigned long)raw_in.consumed, 
                 (unsigned long)frame_out.decoded_size,
                 (unsigned long)s_input_buf_pos, (unsigned long)s_input_buf_len);
    }
    
    /* 处理 BUFF_NOT_ENOUGH - 尝试扩大缓冲区 */
    if (ret == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH && frame_out.needed_size > s_output_buf_size) {
        ESP_LOGI(TAG, "输出缓冲区不足, 需要 %lu 字节", (unsigned long)frame_out.needed_size);
        
        /* 重新分配更大的缓冲区 */
        uint8_t *new_buf = heap_caps_realloc(s_output_buf, frame_out.needed_size, MALLOC_CAP_8BIT);
        if (new_buf) {
            s_output_buf = new_buf;
            s_output_buf_size = frame_out.needed_size;
            
            /* 重新初始化输入输出结构体后重试 */
            raw_in.buffer = s_input_buf + s_input_buf_pos;
            raw_in.len = available;
            raw_in.consumed = 0;
            
            frame_out.buffer = s_output_buf;
            frame_out.len = s_output_buf_size;
            frame_out.decoded_size = 0;
            
            ret = esp_audio_dec_process(s_decoder, &raw_in, &frame_out);
            ESP_LOGI(TAG, "重试解码: ret=%d, consumed=%lu, decoded=%lu", 
                     ret, (unsigned long)raw_in.consumed, (unsigned long)frame_out.decoded_size);
        }
    }
    
    /* 更新消耗位置 */
    if (raw_in.consumed > 0) {
        s_input_buf_pos += raw_in.consumed;
        s_error_count = 0;  /* 成功时重置错误计数 */
    }
    
    /* 错误恢复：当解码持续失败时，尝试重新同步 */
    if (ret != ESP_AUDIO_ERR_OK && ret != ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
        s_error_count++;
        
        /* 连续错误超过阈值，尝试跳过数据找下一个同步字 */
        if (s_error_count > 5 && available > 4) {
            /* 跳过第一个字节，在剩余数据中查找同步字 */
            int sync_pos = find_mp3_sync(s_input_buf + s_input_buf_pos + 1, available - 1);
            if (sync_pos >= 0) {
                int skip_bytes = sync_pos + 1;
                ESP_LOGW(TAG, "错误恢复: 跳过 %d 字节, 重新同步", skip_bytes);
                s_input_buf_pos += skip_bytes;
                s_error_count = 0;
                
                /* 重置解码器状态 */
                esp_audio_dec_reset(s_decoder);
            } else {
                /* 找不到同步字，跳过大块数据 */
                int skip = available > 512 ? 512 : available / 2;
                if (skip > 0) {
                    ESP_LOGW(TAG, "未找到同步字, 跳过 %d 字节", skip);
                    s_input_buf_pos += skip;
                }
            }
        }
        return 0;
    }
    
    /* 获取音频信息并复制到输出 */
    if (frame_out.decoded_size > 0) {
        esp_audio_dec_info_t info;
        if (esp_audio_dec_get_info(s_decoder, &info) == ESP_AUDIO_ERR_OK) {
            if (info.sample_rate > 0) {
                s_sample_rate = info.sample_rate;
            }
            if (info.channel > 0) {
                s_channels = info.channel;
            }
        }
        
        if (sample_rate) *sample_rate = s_sample_rate;
        if (channels) *channels = s_channels;
        
        /* 计算输出采样数 */
        int ch = s_channels > 0 ? s_channels : 2;
        int samples = frame_out.decoded_size / sizeof(int16_t) / ch;
        
        /* 复制到调用者的缓冲区（限制大小） */
        size_t copy_samples = samples;
        if (copy_samples > max_samples) {
            copy_samples = max_samples;
        }
        size_t copy_bytes = copy_samples * ch * sizeof(int16_t);
        memcpy(pcm_out, s_output_buf, copy_bytes);
        
        ESP_LOGI(TAG, "解码成功: %d采样, %dHz, %d声道", samples, s_sample_rate, s_channels);
        return (int)copy_samples;
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
    
    s_input_buf_len = 0;
    s_input_buf_pos = 0;
    s_sync_found = false;
    
    if (s_decoder) {
        esp_audio_dec_reset(s_decoder);
    }
    
    ESP_LOGI(TAG, "解码器已重置");
}

/**
 * @brief       检查解码器是否已初始化
 */
bool mp3_decoder_is_initialized(void)
{
    return s_initialized;
}
