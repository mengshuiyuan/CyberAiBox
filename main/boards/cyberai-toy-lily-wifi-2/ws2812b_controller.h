#ifndef __WS2812B_CONTROLLER_H__
#define __WS2812B_CONTROLLER_H__

#include "mcp_server.h"
#include "application.h"
#include <driver/gpio.h>
#include <led_strip.h>
#include <esp_timer.h>
#include <vector>
#include <functional>

struct RGBColor {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    
    RGBColor(uint8_t r = 0, uint8_t g = 0, uint8_t b = 0) : red(r), green(g), blue(b) {}
};

class WS2812BController {
private:
    gpio_num_t gpio_num_;
    uint8_t led_count_;
    led_strip_handle_t led_strip_;
    esp_timer_handle_t animation_timer_;
    esp_timer_handle_t state_monitor_timer_;
    std::vector<RGBColor> colors_;
    std::function<void()> animation_callback_;
    bool animation_running_;
    bool auto_state_control_;
    float brightness_ = 0.6f; // 亮度系数 (0.0 - 1.0)
    
    // 预设颜色
    static const RGBColor RED;
    static const RGBColor GREEN;
    static const RGBColor BLUE;
    static const RGBColor WHITE;
    static const RGBColor YELLOW;
    static const RGBColor PURPLE;
    static const RGBColor CYAN;
    static const RGBColor ORANGE;
    static const RGBColor PINK;

public:
    WS2812BController(gpio_num_t gpio_num, uint8_t led_count = 2);
    ~WS2812BController();
    
    // 基本控制
    void SetAllColor(RGBColor color);
    void SetSingleLED(uint8_t index, RGBColor color);
    void TurnOff();
    void TurnOn(RGBColor color = WHITE);
    
    // 动画效果
    void StartBreathing(RGBColor color, int interval_ms = 50);
    void StartRainbow(int interval_ms = 100);
    void StartChase(RGBColor color, int interval_ms = 100);
    void StartTwinkle(RGBColor color, int interval_ms = 200);
    void StopAnimation();
    
    // 设备状态控制
    void EnableAutoStateControl(bool enable = true);
    void OnDeviceStateChanged();
    
    // 预设颜色
    static RGBColor GetPresetColor(const std::string& color_name);
    
    // 工具函数
    void RegisterMCPTools();

    void SetBrightness(float brightness);
    float GetBrightness() const { return brightness_; }
    RGBColor ApplyBrightness(RGBColor color) const;
    void RefreshLEDs();

    
private:
    void InitializeLEDStrip();
    void InitializeTimer();
    void InitializeStateMonitor();
    void StartAnimation(int interval_ms, std::function<void()> callback);
    
    // 动画回调函数
    void OnBreathingAnimation();
    void OnRainbowAnimation();
    void OnChaseAnimation();
    void OnTwinkleAnimation();
    
    // 状态监控回调
    void OnStateMonitorTimer();
};

#endif // __WS2812B_CONTROLLER_H__
