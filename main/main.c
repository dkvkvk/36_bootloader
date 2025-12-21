/**
 ****************************************************************************************************
 * @file        main.c
 * @author      Audio Serial Transfer
 * @version     V1.0
 * @date        2024-12-20
 * @brief       ESP32-S3 音频串口传输
 *              - 录音模式：麦克风 → 串口 → 电脑存储
 *              - 播放模式：电脑 → 串口 → 喇叭播放
 ****************************************************************************************************
 */

#include "nvs_flash.h"
#include "iic.h"
#include "xl9555.h"
#include "led.h"
#include "key.h"
#include "es8388.h"
#include "i2s.h"
#include "uart_audio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MAIN";

/* I2C 主机句柄 */
i2c_obj_t i2c0_master;

/**
 * @brief       按键处理任务
 * @note        KEY0: 开始/停止录音 (XL9555 IO扩展)
 *              BOOT: 备用控制
 */
static void key_task(void *arg)
{
    uint8_t key;
    
    while (1) {
        /* 扫描 XL9555 扩展按键 */
        key = xl9555_key_scan(0);
        switch (key) {
            case KEY0_PRES:
                /* KEY0: 切换录音状态 */
                if (uart_audio_get_mode() == MODE_IDLE) {
                    uart_audio_start_record();
                    LED(1);
                    ESP_LOGI(TAG, "按键触发: 开始录音");
                } else if (uart_audio_get_mode() == MODE_RECORDING) {
                    uart_audio_stop_record();
                    LED(0);
                    ESP_LOGI(TAG, "按键触发: 停止录音");
                } else if (uart_audio_get_mode() == MODE_PLAYING) {
                    uart_audio_stop();
                    LED(0);
                    ESP_LOGI(TAG, "按键触发: 停止播放");
                }
                break;
                
            case KEY1_PRES:
                /* KEY1: 停止所有操作 */
                uart_audio_stop_record();
                LED(0);
                ESP_LOGI(TAG, "按键触发: 返回空闲");
                break;
        }
        
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/**
 * @brief       LED状态指示任务
 */
static void led_status_task(void *arg)
{
    while (1) {
        audio_mode_t mode = uart_audio_get_mode();
        
        switch (mode) {
            case MODE_IDLE:
                /* 空闲: LED灭 */
                LED(0);
                vTaskDelay(pdMS_TO_TICKS(500));
                break;
                
            case MODE_RECORDING:
                /* 录音: LED常亮 */
                LED(1);
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
                
            case MODE_PLAYING:
                /* 播放: LED闪烁 */
                LED_TOGGLE();
                vTaskDelay(pdMS_TO_TICKS(200));
                break;
        }
    }
}

/**
 * @brief       程序入口
 */
void app_main(void)
{
    esp_err_t ret;
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "   ESP32-S3 音频串口传输系统");
    ESP_LOGI(TAG, "   采样率: 16kHz, 16bit, 单声道");
    ESP_LOGI(TAG, "========================================");
    
    /* 初始化 NVS */
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    /* 初始化 I2C */
    i2c0_master = iic_init(I2C_NUM_0);
    ESP_LOGI(TAG, "I2C 初始化完成");
    
    /* 初始化 LED */
    led_init();
    LED(0);
    ESP_LOGI(TAG, "LED 初始化完成");
    
    /* 初始化按键 */
    key_init();
    ESP_LOGI(TAG, "KEY 初始化完成");
    
    /* 初始化 IO扩展芯片 (ES8388需要) */
    xl9555_init(i2c0_master);
    ESP_LOGI(TAG, "XL9555 初始化完成");
    
    /* 初始化 ES8388 音频芯片 */
    es8388_init(i2c0_master);
    ESP_LOGI(TAG, "ES8388 初始化完成");
    
    /* 初始化 I2S */
    ret = i2s_init();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "I2S 初始化完成");
    } else {
        ESP_LOGE(TAG, "I2S 初始化失败: %d", ret);
    }
    
    /* 初始化串口音频模块 (使用UART1, TX=GPIO17, RX=GPIO18) */
    /* 注意: UART0 保留给日志输出，不能用于音频传输 */
    ret = uart_audio_init(UART_NUM_1, 17, 18);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "UART音频模块初始化完成");
    } else {
        ESP_LOGE(TAG, "UART音频模块初始化失败: %d", ret);
    }
    
    /* 启动音频处理任务 */
    uart_audio_start();
    ESP_LOGI(TAG, "音频处理任务已启动");
    
    /* 创建按键处理任务 */
    xTaskCreate(key_task, "key_task", 2048, NULL, 5, NULL);
    
    /* 创建LED状态任务 */
    xTaskCreate(led_status_task, "led_status", 2048, NULL, 3, NULL);
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "   系统启动完成!");
    ESP_LOGI(TAG, "   KEY0: 开始/停止录音");
    ESP_LOGI(TAG, "   KEY1: 返回空闲模式");
    ESP_LOGI(TAG, "   音频串口: UART1 (TX=17, RX=18)");
    ESP_LOGI(TAG, "   日志串口: UART0 (USB)");
    ESP_LOGI(TAG, "========================================");
}
