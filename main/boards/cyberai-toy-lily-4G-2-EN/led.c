#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "led.h"

#define WS2812_GPIO GPIO_NUM_15 
#define LED_COUNT 4
#define TAG "RGB"
// Color definitions 
typedef struct { 
    uint8_t red; 
    uint8_t green; 
    uint8_t blue; 
} ws2812_color_t; 

// Color palette 
const ws2812_color_t colors[] = { 
    {255, 0, 0},   // Red 
    {0, 255, 0},   // Green 
    {0, 0, 255},   // Blue 
    {255, 255, 0}, // Yellow 
    {255, 0, 255}, // Magenta 
    {0, 255, 255}, // Cyan 
    {255, 255, 255} // White 
}; 

#define COLOR_COUNT (sizeof(colors) / sizeof(colors[0]))

void led_task(void *arg) 
{
    static int color_index = 0;  // 添加这个变量
    
    rmt_channel_handle_t tx_channel = NULL;
    rmt_encoder_handle_t ws2812_encoder = NULL;
    
    // 1. 配置 RMT 通道
    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = WS2812_GPIO,
        .mem_block_symbols = 64,
        .resolution_hz = 10000000, // 10MHz, 0.1us per tick
        .trans_queue_depth = 4,
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &tx_channel));
    
    // 2. 配置 WS2812 编码器 - 更简单的方式
    // 首先创建字节编码器
    rmt_bytes_encoder_config_t bytes_encoder_config = {
        .bit0 = {
            .level0 = 1,
            .duration0 = 3,  // 0.3us * 10MHz = 3 ticks
            .level1 = 0,
            .duration1 = 9,  // 0.9us * 10MHz = 9 ticks
        },
        .bit1 = {
            .level0 = 1,
            .duration0 = 9,  // 0.9us * 10MHz = 9 ticks
            .level1 = 0,
            .duration1 = 3,  // 0.3us * 10MHz = 3 ticks
        },
    };
    ESP_ERROR_CHECK(rmt_new_bytes_encoder(&bytes_encoder_config, &ws2812_encoder));
    
    // 3. 启用通道
    ESP_ERROR_CHECK(rmt_enable(tx_channel));
    
    // 4. 创建传输配置
    rmt_transmit_config_t tx_config = {
        .loop_count = 0, // 不循环
    };
    
    uint8_t led_buffer[LED_COUNT * 3]; // RGB 数据
    
    while (1) {
        // 填充颜色数据
        ws2812_color_t current_color = colors[color_index];
        
        for (int i = 0; i < LED_COUNT; i++) {
            led_buffer[i*3 + 0] = current_color.green;  // WS2812 顺序是 GRB
            led_buffer[i*3 + 1] = current_color.red;
            led_buffer[i*3 + 2] = current_color.blue;
        }
        
        // 发送数据
        ESP_ERROR_CHECK(rmt_transmit(tx_channel, ws2812_encoder, led_buffer, 
                                     sizeof(led_buffer), &tx_config));
        ESP_ERROR_CHECK(rmt_tx_wait_all_done(tx_channel, portMAX_DELAY));
        
        // 更新颜色索引
        color_index = (color_index + 1) % COLOR_COUNT;
        //ESP_LOGI(TAG,"----RGB变色----");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    // 清理（实际不会执行到这里）
    ESP_ERROR_CHECK(rmt_disable(tx_channel));
    ESP_ERROR_CHECK(rmt_del_encoder(ws2812_encoder));
    ESP_ERROR_CHECK(rmt_del_channel(tx_channel));
    vTaskDelete(NULL);
}