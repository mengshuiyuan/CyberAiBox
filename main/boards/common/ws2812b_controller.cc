#include "ws2812b_controller.h"
#include <esp_log.h>
#include <cmath>

#define TAG "WS2812BController"

// 预设颜色定义
const RGBColor WS2812BController::RED(0, 255, 0);
const RGBColor WS2812BController::GREEN(255, 0, 0);
const RGBColor WS2812BController::BLUE(0, 0, 255);
const RGBColor WS2812BController::WHITE(255, 255, 255);
const RGBColor WS2812BController::YELLOW(255, 255, 0);
const RGBColor WS2812BController::PURPLE(128, 0, 128);  // 修正为标准紫色
const RGBColor WS2812BController::CYAN(0, 255, 255);
const RGBColor WS2812BController::ORANGE(255, 165, 0);  // 或 (255, 140, 0)
const RGBColor WS2812BController::PINK(255, 192, 203);

WS2812BController::WS2812BController(gpio_num_t gpio_num, uint8_t led_count) 
    : gpio_num_(gpio_num), led_count_(led_count), animation_running_(false), auto_state_control_(false) {
    
    colors_.resize(led_count_);
    InitializeLEDStrip();
    InitializeTimer();
    InitializeStateMonitor();
    RegisterMCPTools();
    
    ESP_LOGI(TAG, "WS2812B Controller initialized on GPIO %d with %d LEDs", gpio_num_, led_count_);
}

WS2812BController::~WS2812BController() {
    StopAnimation();
    if (led_strip_) {
        led_strip_del(led_strip_);
    }
}

void WS2812BController::InitializeLEDStrip() {
    led_strip_config_t strip_config = {};
    strip_config.strip_gpio_num = gpio_num_;
    strip_config.max_leds = led_count_;
    strip_config.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_RGB;
    strip_config.led_model = LED_MODEL_WS2812;

    led_strip_rmt_config_t rmt_config = {};
    rmt_config.resolution_hz = 10 * 1000 * 1000; // 10MHz

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip_));
    led_strip_clear(led_strip_);
    ESP_LOGI(TAG,"----1----");
}

void WS2812BController::InitializeTimer() {
    esp_timer_create_args_t timer_args = {
        .callback = [](void *arg) {
            auto controller = static_cast<WS2812BController*>(arg);
            if (controller->animation_callback_) {
                controller->animation_callback_();
            }
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "ws2812b_animation",
        .skip_unhandled_events = false,
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &animation_timer_));
    ESP_LOGI(TAG,"----2----");
}

void WS2812BController::InitializeStateMonitor() {
    esp_timer_create_args_t state_timer_args = {
        .callback = [](void *arg) {
            auto controller = static_cast<WS2812BController*>(arg);
            controller->OnStateMonitorTimer();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "ws2812b_state_monitor",
        .skip_unhandled_events = false,
    };
    ESP_ERROR_CHECK(esp_timer_create(&state_timer_args, &state_monitor_timer_));
    ESP_LOGI(TAG,"----3----");
}

void WS2812BController::SetAllColor(RGBColor color) {
    StopAnimation();
    for (int i = 0; i < led_count_; i++) {
        colors_[i] = color;
        RGBColor adjusted_color = ApplyBrightness(color);
        led_strip_set_pixel(led_strip_, i, 
                           adjusted_color.red, 
                           adjusted_color.green, 
                           adjusted_color.blue);
    }
    led_strip_refresh(led_strip_);
    //ESP_LOGI(TAG, "Set all LEDs to RGB(%d, %d, %d)", color.red, color.green, color.blue);
}

void WS2812BController::SetSingleLED(uint8_t index, RGBColor color) {
    if (index >= led_count_) {
        ESP_LOGW(TAG, "LED index %d out of range (max: %d)", index, led_count_ - 1);
        return;
    }
    
    StopAnimation();
    colors_[index] = color;
    RGBColor adjusted_color = ApplyBrightness(color);
    led_strip_set_pixel(led_strip_, index, 
                       adjusted_color.red, 
                       adjusted_color.green, 
                       adjusted_color.blue);
    led_strip_refresh(led_strip_);
}
void WS2812BController::TurnOff() {
    SetAllColor(RGBColor(0, 0, 0));
}

void WS2812BController::TurnOn(RGBColor color) {
    SetAllColor(color);
}

void WS2812BController::StartBreathing(RGBColor color, int interval_ms) {
    StartAnimation(interval_ms, [this, color]() {
        static float brightness = 0.0f;
        static bool increasing = true;
        
        if (increasing) {
            brightness += 0.05f;
            if (brightness >= 1.0f) {
                brightness = 1.0f;
                increasing = false;
            }
        } else {
            brightness -= 0.05f;
            if (brightness <= 0.0f) {
                brightness = 0.0f;
                increasing = true;
            }
        }
        
        RGBColor current_color(
            static_cast<uint8_t>(color.red * brightness),
            static_cast<uint8_t>(color.green * brightness),
            static_cast<uint8_t>(color.blue * brightness)
        );
        
        for (int i = 0; i < led_count_; i++) {
            led_strip_set_pixel(led_strip_, i, current_color.red, current_color.green, current_color.blue);
        }
        led_strip_refresh(led_strip_);
    });
}

void WS2812BController::StartRainbow(int interval_ms) {
    StartAnimation(interval_ms, [this]() {
        static float hue = 0.0f;
        
        for (int i = 0; i < led_count_; i++) {
            float led_hue = hue + (i * 360.0f / led_count_);
            if (led_hue >= 360.0f) led_hue -= 360.0f;
            
            // HSV to RGB conversion
            float h = led_hue / 60.0f;
            int hi = static_cast<int>(h) % 6;
            float f = h - static_cast<int>(h);
            float v = 1.0f;
            float s = 1.0f;
            
            float p = v * (1.0f - s);
            float q = v * (1.0f - f * s);
            float t = v * (1.0f - (1.0f - f) * s);
            
            float r, g, b;
            switch (hi) {
                case 0: r = v; g = t; b = p; break;
                case 1: r = q; g = v; b = p; break;
                case 2: r = p; g = v; b = t; break;
                case 3: r = p; g = q; b = v; break;
                case 4: r = t; g = p; b = v; break;
                default: r = v; g = p; b = q; break;
            }
            
            led_strip_set_pixel(led_strip_, i, 
                static_cast<uint8_t>(r * 255), 
                static_cast<uint8_t>(g * 255), 
                static_cast<uint8_t>(b * 255));
        }
        led_strip_refresh(led_strip_);
        
        hue += 2.0f;
        if (hue >= 360.0f) hue -= 360.0f;
    });
}

void WS2812BController::StartChase(RGBColor color, int interval_ms) {
    StartAnimation(interval_ms, [this, color]() {
        static int position = 0;
        
        // 清除所有LED
        for (int i = 0; i < led_count_; i++) {
            led_strip_set_pixel(led_strip_, i, 0, 0, 0);
        }
        
        // 设置当前位置的LED
        led_strip_set_pixel(led_strip_, position, color.red, color.green, color.blue);
        led_strip_refresh(led_strip_);
        
        position = (position + 1) % led_count_;
    });
}

void WS2812BController::StartTwinkle(RGBColor color, int interval_ms) {
    StartAnimation(interval_ms, [this, color]() {
        static int counter = 0;
        
        // 每10次更新随机闪烁一个LED
        if (counter % 10 == 0) {
            int random_led = rand() % led_count_;
            bool turn_on = rand() % 2;
            
            if (turn_on) {
                led_strip_set_pixel(led_strip_, random_led, color.red, color.green, color.blue);
            } else {
                led_strip_set_pixel(led_strip_, random_led, 0, 0, 0);
            }
            led_strip_refresh(led_strip_);
        }
        
        counter++;
    });
}

void WS2812BController::StopAnimation() {
    if (animation_running_) {
        esp_timer_stop(animation_timer_);
        animation_running_ = false;
        animation_callback_ = nullptr;
    }
}

void WS2812BController::StartAnimation(int interval_ms, std::function<void()> callback) {
    StopAnimation();
    animation_callback_ = callback;
    animation_running_ = true;
    esp_timer_start_periodic(animation_timer_, interval_ms * 1000);
}

void WS2812BController::EnableAutoStateControl(bool enable) {
    auto_state_control_ = enable;
    if (enable) {
        // 启动状态监控定时器，每500ms检查一次设备状态
        esp_timer_start_periodic(state_monitor_timer_, 500000); // 500ms
        ESP_LOGI(TAG, "Auto state control enabled");
        OnDeviceStateChanged(); // 立即应用当前状态
    } else {
        esp_timer_stop(state_monitor_timer_);
        ESP_LOGI(TAG, "Auto state control disabled");
    }
}

void WS2812BController::OnDeviceStateChanged() {
    if (!auto_state_control_) {
        return;
    }
    
    auto& app = Application::GetInstance();
    auto device_state = app.GetDeviceState();
    
    //ESP_LOGI(TAG, "Device state changed to: %d", device_state);
    
    switch (device_state) {
        case kDeviceStateStarting:
        case kDeviceStateWifiConfiguring:
        case kDeviceStateConnecting:
        case kDeviceStateUpgrading:
        case kDeviceStateActivating:
            // 初始化阶段：亮蓝光
            SetAllColor(YELLOW);
            //ESP_LOGI(TAG, "Setting ambient light to BLUE (initialization)");
            break;
            
        case kDeviceStateIdle:
            // 激活后：不亮
        SetAllColor(BLUE);    
            //TurnOff();
            //ESP_LOGI(TAG, "Turning off ambient light (idle)");
            break;
            
        case kDeviceStateListening:
        case kDeviceStateAudioTesting:
            // 聆听时：亮绿光
            SetAllColor(GREEN);
            //ESP_LOGI(TAG, "Setting ambient light to GREEN (listening)");
            break;
            
        case kDeviceStateSpeaking:
            // 说话时：亮粉光
            SetAllColor(PINK);
            //ESP_LOGI(TAG, "Setting ambient light to RED (speaking)");
            break;
            
        default:
            ESP_LOGW(TAG, "Unknown device state: %d", device_state);
            break;
    }
}

void WS2812BController::OnStateMonitorTimer() {
    OnDeviceStateChanged();
}

RGBColor WS2812BController::GetPresetColor(const std::string& color_name) {
    if (color_name == "red") return RED;
    if (color_name == "green") return GREEN;
    if (color_name == "blue") return BLUE;
    if (color_name == "white") return WHITE;
    if (color_name == "yellow") return YELLOW;
    if (color_name == "purple") return PURPLE;
    if (color_name == "cyan") return CYAN;
    if (color_name == "orange") return ORANGE;
    if (color_name == "pink") return PINK;
    
    return WHITE; // 默认白色
}

void WS2812BController::RegisterMCPTools() {
    auto& mcp_server = McpServer::GetInstance();
    
    // 获取当前状态
    // mcp_server.AddTool("self.ambient_light.get_state", 
    //     "Get the current state of ambient light", 
    //     PropertyList(), 
    //     [this](const PropertyList& properties) -> ReturnValue {
    //         return "{\"animation_running\": " + std::string(animation_running_ ? "true" : "false") + 
    //                ", \"led_count\": " + std::to_string(led_count_) + 
    //                ", \"auto_control\": " + std::string(auto_state_control_ ? "true" : "false") + "}";
    //     });
    
    // 设置所有LED颜色
    mcp_server.AddTool("self.ambient_light.set_color", 
        "Set all LEDs to a specific color", 
        PropertyList({
            Property("red", kPropertyTypeInteger, 0, 255),
            Property("green", kPropertyTypeInteger, 0, 255),
            Property("blue", kPropertyTypeInteger, 0, 255)
        }), 
        [this](const PropertyList& properties) -> ReturnValue {
            int red = properties["red"].value<int>();
            int green = properties["green"].value<int>();
            int blue = properties["blue"].value<int>();
            SetAllColor(RGBColor(red, green, blue));
            return true;
        });
    
    // 设置预设颜色
    // mcp_server.AddTool("self.ambient_light.set_preset_color", 
    //     "Set all LEDs to a preset color (red, green, blue, white, yellow, purple, cyan, orange, pink)", 
    //     PropertyList({
    //         Property("color", kPropertyTypeString)
    //     }), 
    //     [this](const PropertyList& properties) -> ReturnValue {
    //         std::string color_name = properties["color"].value<std::string>();
    //         SetAllColor(GetPresetColor(color_name));
    //         return true;
    //     });
    
    // 关闭所有LED
    mcp_server.AddTool("self.ambient_light.turn_off", 
        "Turn off all LEDs", 
        PropertyList(), 
        [this](const PropertyList& properties) -> ReturnValue {
            TurnOff();
            return true;
        });
    
    // 呼吸灯效果
    // mcp_server.AddTool("self.ambient_light.start_breathing", 
    //     "Start breathing animation with specified color", 
    //     PropertyList({
    //         Property("red", kPropertyTypeInteger, 0, 255),
    //         Property("green", kPropertyTypeInteger, 0, 255),
    //         Property("blue", kPropertyTypeInteger, 0, 255),
    //         Property("interval_ms", kPropertyTypeInteger, 20, 500)
    //     }), 
    //     [this](const PropertyList& properties) -> ReturnValue {
    //         int red = properties["red"].value<int>();
    //         int green = properties["green"].value<int>();
    //         int blue = properties["blue"].value<int>();
    //         int interval = properties["interval_ms"].value<int>();
    //         StartBreathing(RGBColor(red, green, blue), interval);
    //         return true;
    //     });
    
    // 彩虹效果
    // mcp_server.AddTool("self.ambient_light.start_rainbow", 
    //     "Start rainbow animation", 
    //     PropertyList({
    //         Property("interval_ms", kPropertyTypeInteger, 50, 500)
    //     }), 
    //     [this](const PropertyList& properties) -> ReturnValue {
    //         int interval = properties["interval_ms"].value<int>();
    //         StartRainbow(interval);
    //         return true;
    //     });
    
    // 追逐效果
    // mcp_server.AddTool("self.ambient_light.start_chase", 
    //     "Start chase animation with specified color", 
    //     PropertyList({
    //         Property("red", kPropertyTypeInteger, 0, 255),
    //         Property("green", kPropertyTypeInteger, 0, 255),
    //         Property("blue", kPropertyTypeInteger, 0, 255),
    //         Property("interval_ms", kPropertyTypeInteger, 50, 500)
    //     }), 
    //     [this](const PropertyList& properties) -> ReturnValue {
    //         int red = properties["red"].value<int>();
    //         int green = properties["green"].value<int>();
    //         int blue = properties["blue"].value<int>();
    //         int interval = properties["interval_ms"].value<int>();
    //         StartChase(RGBColor(red, green, blue), interval);
    //         return true;
    //     });
    
    // 闪烁效果
    // mcp_server.AddTool("self.ambient_light.start_twinkle", 
    //     "Start twinkle animation with specified color", 
    //     PropertyList({
    //         Property("red", kPropertyTypeInteger, 0, 255),
    //         Property("green", kPropertyTypeInteger, 0, 255),
    //         Property("blue", kPropertyTypeInteger, 0, 255),
    //         Property("interval_ms", kPropertyTypeInteger, 100, 1000)
    //     }), 
    //     [this](const PropertyList& properties) -> ReturnValue {
    //         int red = properties["red"].value<int>();
    //         int green = properties["green"].value<int>();
    //         int blue = properties["blue"].value<int>();
    //         int interval = properties["interval_ms"].value<int>();
    //         StartTwinkle(RGBColor(red, green, blue), interval);
    //         return true;
    //     });
    
    // 停止动画
    // mcp_server.AddTool("self.ambient_light.stop_animation", 
    //     "Stop current animation", 
    //     PropertyList(), 
    //     [this](const PropertyList& properties) -> ReturnValue {
    //         StopAnimation();
    //         return true;
    //     });
    
    // 启用/禁用自动状态控制
    mcp_server.AddTool("self.ambient_light.enable_auto_control", 
        "Enable automatic state-based control", 
        PropertyList({
            Property("enable", kPropertyTypeBoolean)
        }), 
        [this](const PropertyList& properties) -> ReturnValue {
            bool enable = properties["enable"].value<bool>();
            EnableAutoStateControl(enable);
            return true;
        });
    //设置灯光亮度
        mcp_server.AddTool("self.ambient_light.set_brightness", 
        "设置灯光的亮度", 
        PropertyList({
            Property("brightness", kPropertyTypeInteger, 0, 100),
        }), 
        [this](const PropertyList& properties) -> ReturnValue {
            int brightness = properties["brightness"].value<int>();
            float normalized_brightness = (float)brightness / 100.0f;
            SetBrightness(normalized_brightness);
            return true;
        });
}
void WS2812BController::SetBrightness(float brightness) {
    brightness_ = std::max(0.0f, std::min(1.0f, brightness));
    RefreshLEDs(); // 刷新所有LED以应用新的亮度
}


void WS2812BController::RefreshLEDs() {
    for (int i = 0; i < led_count_; i++) {
        RGBColor adjusted_color = ApplyBrightness(colors_[i]);
        led_strip_set_pixel(led_strip_, i, 
                           adjusted_color.red, 
                           adjusted_color.green, 
                           adjusted_color.blue);
    }
    led_strip_refresh(led_strip_);
}

RGBColor WS2812BController::ApplyBrightness(RGBColor color) const {
    return RGBColor(
        static_cast<uint8_t>(color.red * brightness_),
        static_cast<uint8_t>(color.green * brightness_),
        static_cast<uint8_t>(color.blue * brightness_)
    );
}