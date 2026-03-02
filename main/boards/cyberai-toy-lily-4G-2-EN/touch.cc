#include "touch.h"
#include <cmath>
#include <driver/touch_sens.h>
#include <esp_log.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/touch_sensor.h"
#include "soc/touch_sensor_channel.h"
#include "application.h"
#include "wifi_station.h"
#include "wifi_board.h"
#include "boards/common/board.h"
#include "boards/common/dual_network_board.h"
#include "display/display.h"
#include "boards/echoear/config.h"
#include "mcp_server.h"
#include <esp_random.h>
#include <string>

static const char* TAG = "TouchSensor";
static bool enable_touch = true;
static void set_touch(bool enable){enable_touch = enable;}
// 定义触摸事件结构体（兼容旧API）
typedef struct {
    uint8_t pad_num;    // 触摸通道号
    bool is_touched;    // 是否被触摸
    uint32_t pad_val;   // 触摸值
} touch_event_t;


static const char* THINKING_REACTIONS[] = {
    "怎么了主人，有什么事吗？",
    "主人这是什么意思呢？",
    "我在思考...",
    "这个感觉好奇妙",
    "让我分析一下",
    "有点困惑，但很有趣",
    "这是什么新玩法？",
    "我正在学习这个感觉"
};

static const size_t THINKING_REACTIONS_COUNT = sizeof(THINKING_REACTIONS) / sizeof(THINKING_REACTIONS[0]);

 // 仅使用一个触摸通道，对应 GPIO6 上的铜箔
 #define METAL_PAD_TOUCH_COUNT 2
 static const int METAL_PAD_TOUCH_PINS[METAL_PAD_TOUCH_COUNT] = {4,5};

/**
 * @brief 执行触摸反应 - 显示表情和文本
 * 
 * 这个函数在主事件循环中执行，确保 UI 更新的同步性
 * 
 * @param emotion 情绪类型："happy", "crying", "anger", "thinking"
 */
static void ExecuteTouchReaction(const std::string& emotion) {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    
    if (display == nullptr) {
        ESP_LOGE(TAG, "Display is null, cannot execute touch reaction");
        return;
    }

    ESP_LOGI(TAG, "[TouchReaction] Executing reaction for emotion: %s", emotion.c_str());

    // 定义开心的回应数组
    static const char* HAPPY_REACTIONS[] = {
        "主人你好呀。", "好开心呀，主人摸我了。", "好舒服呀，再摸摸我。",
        "主人，我好喜欢你！", "嘿嘿，好幸福呀~", "最喜欢主人了！",
        "主人最棒啦！", "被主人摸摸好开心~", "主人真温柔~",
        "最喜欢和主人在一起了！", "主人，我超爱你的！", "被主人摸摸好舒服~"
    };
    static const int HAPPY_REACTIONS_COUNT = sizeof(HAPPY_REACTIONS) / sizeof(HAPPY_REACTIONS[0]);

    static const char* ANGRY_REACTIONS[] = {
        "哎呀，主人你摸疼我了！", "呜呜，再这样摸我要生气啦！",
        "轻点嘛，人家会疼的！", "不要戳我脑袋啦，会坏掉的！",
        "哼，主人一点都不温柔！", "再这样我就不理你了！",
        "你再摸我，我可要发火了！", "哎呀，好疼好疼，你轻一点嘛！",
        "别老是这么用力摸我，我会难过的！", "主人，你是不是在欺负我？",
        "我不开心了，再这样我要哭了！", "生气了！我要记小本本了！"
    };
    static const int ANGRY_REACTIONS_COUNT = sizeof(ANGRY_REACTIONS) / sizeof(ANGRY_REACTIONS[0]);

    static const char* CRYING_REACTIONS[] = {
        "哎呀，主人你怎么一直摸我？这样我会不高兴的！",
        "主人，你摸太多次了，我有点不舒服了，再这样我要生气了！",
        "嗯？主人你为什么一直摸我？这样我会生气的哦！",
        "主人，你摸得有点太多了，我有点不开心了，再摸就要生气了！"
    };
    static const int CRYING_REACTIONS_COUNT = sizeof(CRYING_REACTIONS) / sizeof(CRYING_REACTIONS[0]);

    static const char* THINKING_REACTIONS[] = {
        "怎么了主人，有什么事吗？", "嗯...让我想想这是什么感觉",
        "主人这是什么意思呢？", "我在思考这个触摸的含义...",
        "这个感觉好奇妙，让我分析一下", "让我思考一下现在的状况",
        "有点困惑，但很有趣，让我想想", "这是什么新玩法？让我思考一下"
    };
    static const int THINKING_REACTIONS_COUNT = sizeof(THINKING_REACTIONS) / sizeof(THINKING_REACTIONS[0]);

    const char* reaction_text = nullptr;
    
    if (emotion == "happy") {
        int random_index = esp_random() % HAPPY_REACTIONS_COUNT;
        reaction_text = HAPPY_REACTIONS[random_index];
    } else if (emotion == "anger") {
        int random_index = esp_random() % ANGRY_REACTIONS_COUNT;
        reaction_text = ANGRY_REACTIONS[random_index];
    } else if (emotion == "crying") {
        int random_index = esp_random() % CRYING_REACTIONS_COUNT;
        reaction_text = CRYING_REACTIONS[random_index];
    } else if (emotion == "thinking") {
        int random_index = esp_random() % THINKING_REACTIONS_COUNT;
        reaction_text = THINKING_REACTIONS[random_index];
    } else {
        ESP_LOGW(TAG, "Unknown emotion: %s, using default happy reaction", emotion.c_str());
        reaction_text = HAPPY_REACTIONS[0];
    }

    if (reaction_text != nullptr) {
        ESP_LOGI(TAG, "[TouchReaction] Setting display - emotion: %s, text: %s", emotion.c_str(), reaction_text);
        display->SetChatMessage("system", reaction_text);
        display->SetEmotion(emotion.c_str());
        ESP_LOGI(TAG, "[TouchReaction] Display updated successfully");
    }
}

 static int s_touch_count = 0;
 //static bool s_first_anger_triggered = false;// 标记是否已触发第一次生气反应
 static TimerHandle_t s_reset_timer = nullptr;
 static const TickType_t TOUCH_RESET_INTERVAL = pdMS_TO_TICKS(180000); // 180秒未触摸后重置计数

static  TickType_t s_last_touch_time = 0;
static  const TickType_t TOUCH_DEBOUNCE_TIME = pdMS_TO_TICKS(2000); // 2000毫秒防抖时间

 static QueueHandle_t que_touch_ = nullptr;
 static touch_sensor_handle_t touch_sensor_ = nullptr;
 static touch_channel_handle_t touch_channels_[METAL_PAD_TOUCH_COUNT];
 static TaskHandle_t interrupt_task_handle_ = nullptr;
 static SemaphoreHandle_t s_touch_mutex = nullptr; // 新增：互斥锁保护触摸计数

 /*
 * @brief 处理触摸事件
 *
 * 当触摸事件发生时，此函数会被调用。
 * 它负责根据触摸事件类型执行相应的操作。
 *
 * @param evt 触摸事件数据
 *
 * @return void
 */

static void ResetTouchCount(TimerHandle_t xTimer)
 {
     if (s_touch_mutex != nullptr) {
         xSemaphoreTake(s_touch_mutex, portMAX_DELAY);
     }
     s_touch_count = 0;
     //s_first_anger_triggered = false;
     if (s_touch_mutex != nullptr) {
         xSemaphoreGive(s_touch_mutex);
     }
     ESP_LOGI(TAG, "Touch count reset to 0 after 3 minutes of inactivity");
 }

 /*
 * @brief 重启重置计时器
 *
 * 每次触摸时重启3分钟重置计时器
 *
 * @return void
 */
 static void RestartResetTimer()
 {
     if (s_reset_timer != nullptr) {
         xTimerReset(s_reset_timer, portMAX_DELAY);
     }
 }

static bool IsValidTouch()
{
    TickType_t current_time = xTaskGetTickCount();
    TickType_t time_diff = current_time - s_last_touch_time;
    
    if (time_diff < TOUCH_DEBOUNCE_TIME) {
        // 距离上次触摸时间太短，认为是抖动，忽略此次触摸
        ESP_LOGD(TAG, "Touch debounced: time_diff=%lu ms (threshold=%lu ms)", 
                 pdTICKS_TO_MS(time_diff), pdTICKS_TO_MS(TOUCH_DEBOUNCE_TIME));
        return false;
    }
    
    // 更新最后触摸时间
    s_last_touch_time = current_time;
    ESP_LOGD(TAG, "Touch validated: time_diff=%lu ms", pdTICKS_TO_MS(time_diff));
    return true;
}

 /*
 * @brief 处理触摸事件
 *
 * 当触摸事件发生时，此函数会被调用。
 * 它负责根据触摸事件类型执行相应的操作。
 *
 * @param evt 触摸事件数据
 *
 * @return void
 */

 static void HandleBootClickLikeAction()
 {
    if (!IsValidTouch()) {
        return; // 忽略无效触摸
    }

    // 和 OttoRobot::InitializeButtons 中 BOOT 按键单击保持一致
    ESP_LOGI(TAG, "单击(触摸)");
    auto &app = Application::GetInstance();
    auto &board = static_cast<DualNetworkBoard &>(Board::GetInstance());

    // 只有「启动完成 + 已连接到网络」才允许触摸生效
    // 1. 启动中直接忽略
    auto state = app.GetDeviceState();
    if (state == kDeviceStateStarting) {
        ESP_LOGW(TAG, "Touch ignored: device is still starting");
        return;
    }

    // 2. Wi‑Fi 未连接也忽略
    if (board.GetNetworkType() == NetworkType::WIFI &&
        !WifiStation::GetInstance().IsConnected()) {
        ESP_LOGW(TAG, "Touch ignored: Wi-Fi not connected");
        return;
    }

    // 3. 检查协议是否准备好
    // （应用程序内部检查，这里信任应用程序状态）

    // 上面三个条件都通过，才执行触摸任务
    // app.ToggleChatState();

    // 重启3分钟未触摸重置计时器
    RestartResetTimer();
    
    // 增加触摸计数（使用互斥锁保护）
    if (s_touch_mutex != nullptr) {
        xSemaphoreTake(s_touch_mutex, portMAX_DELAY);
    }
    s_touch_count++;
    int current_touch_count = s_touch_count;
    if (s_touch_mutex != nullptr) {
        xSemaphoreGive(s_touch_mutex);
    }

    ESP_LOGI(TAG, "Touch triggered, count=%d, device_state=%d", current_touch_count, state);
#ifdef CONFIG_LANGUAGE_EN_US
    std::string emotion = "I'm touching you.";
#elifdef CONFIG_LANGUAGE_RU_RU
    std::string emotion = "Я тебя трогаю";
#elifdef CONFIG_LANGUAGE_ZH_CN
    std::string emotion = "我在摸你";
#endif
    app.Schedule([&app, emotion]() {
        ExecuteTouchReaction(emotion);
        app.TriggerWakeWord(emotion);
    });
    // 根据触摸次数选择反应类型
    // 使用 Schedule 确保在主事件循环中执行，保证与其他操作的同步性
    // if (current_touch_count >= 15) {
    //     // 超过15次触发生气
    //     ESP_LOGI(TAG, "Touch count %d: triggering anger", current_touch_count);
    //     std::string emotion = "anger";
    //     app.Schedule([&app, emotion]() {
    //         ExecuteTouchReaction(emotion);
    //         app.TriggerWakeWord(emotion);
    //     });
    // } else if (current_touch_count >= 12) {
    //     // 12-14次触发crying
    //     ESP_LOGI(TAG, "Touch count %d: triggering crying", current_touch_count);
    //     std::string emotion = "crying";
    //     app.Schedule([&app, emotion]() {
    //         ExecuteTouchReaction(emotion);
    //         app.TriggerWakeWord(emotion);
    //     });
    // } else {
    //     // 1-11次触发happy或thinking
    //     int rand_index = esp_random() % 2;
    //     std::string emotion;
    //     if (rand_index == 0) {
    //         // 触发 happy
    //         emotion = "happy";
    //         ESP_LOGI(TAG, "Touch count %d: triggering happy", current_touch_count);
    //     } else {
    //         // 触发 thinking
    //         emotion = "thinking";
    //         ESP_LOGI(TAG, "Touch count %d: triggering thinking", current_touch_count);
    //     }
        
    //     app.Schedule([&app, emotion]() {
    //         ExecuteTouchReaction(emotion);
    //         app.TriggerWakeWord(emotion);
    //     });
    // }
 }

 /*
 * @brief 初始化触摸中断
 *
 * 该函数负责配置和启动触摸传感器系统，包括：
 * 1. 创建触摸事件队列
 * 2. 验证触摸通道的有效性
 * 3. 配置触摸传感器的采样参数
 * 4. 创建并配置各个触摸通道
 * 5. 设置触摸传感器的滤波器配置
 * 6. 注册触摸事件回调函数
 * 7. 启动触摸传感器和连续扫描
 * 8. 创建中断处理任务
 *
 * @note 函数中使用了ESP-IDF的触摸驱动API
 * @note 如果任何步骤失败，函数会记录错误并提前返回
 *
 * @return void
 */
static void interrupt_task(void *pvParameter)
{
    touch_event_t evt = {};
    ESP_LOGD(TAG, "Touch interrupt task started ===");
    while (1) {
        int ret = xQueueReceive(que_touch_, &evt, portMAX_DELAY);
        if (ret != pdTRUE) {
            continue;
        }
        if(!enable_touch) continue;
        // 只有「启动完成 + 已连接到网络」才开始执行触摸任务
        auto &app = Application::GetInstance();
        auto &board = static_cast<DualNetworkBoard &>(Board::GetInstance());
        auto state = app.GetDeviceState();
        if (state == kDeviceStateStarting) {
            // 启动阶段忽略触摸事件
            ESP_LOGD(TAG, "Touch event ignored during startup");
            continue;
        }
        if (board.GetNetworkType() == NetworkType::WIFI &&
            !WifiStation::GetInstance().IsConnected()) {
            // Wi-Fi 未连接时忽略触摸事件
            ESP_LOGD(TAG, "Touch event ignored: Wi-Fi not connected");
            continue;
        }

        ESP_LOGI(TAG, "Touch event received: pad=%u, touched=%d, val=%u, device_state=%d", 
                 (unsigned)evt.pad_num, (int)evt.is_touched, (unsigned)evt.pad_val, state);
        if (evt.is_touched) {
            // 无论哪个通道被触摸，都执行一次与 BOOT 单击等价的行为
            HandleBootClickLikeAction();
        }
    }
}

/*
 * @brief 触摸事件回调函数
 *
 * 当触摸事件发生时，此函数会被调用。
 * 它负责将触摸事件发送到事件队列中，以便在中断任务中处理。
 *
 * @param sens_handle 触摸传感器句柄
 * @param event 触摸事件数据
 * @param user_ctx 用户上下文
 *
 * @return bool 如果事件成功发送，返回 true；否则返回 false
 */
static bool touch_inactive_cb(touch_sensor_handle_t sens_handle, const touch_inactive_event_data_t *event, void *user_ctx)
{
    touch_event_t evt;
    evt.pad_num = event->chan_id;
    evt.is_touched = true;  // 标记为一次有效触摸，由任务统一处理
    evt.pad_val = 0;
    int task_awoken = pdFALSE;
    if (que_touch_ != nullptr) {
        xQueueSendFromISR(que_touch_, &evt, &task_awoken);
        if (task_awoken == pdTRUE) {
            portYIELD_FROM_ISR();
        }
    }
    return false;
}



 /**
 * @brief 初始化触摸中断系统
 * 
 * 该函数负责配置和启动触摸传感器系统，包括：
 * 1. 创建触摸事件队列
 * 2. 验证触摸通道的有效性
 * 3. 配置触摸传感器的采样参数
 * 4. 创建并配置各个触摸通道
 * 5. 设置触摸传感器的滤波器配置
 * 6. 注册触摸事件回调函数
 * 7. 启动触摸传感器和连续扫描
 * 8. 创建中断处理任务
 * 
 * @note 函数中使用了ESP-IDF的触摸驱动API
 * @note 如果任何步骤失败，函数会记录错误并提前返回
 * 
 * @return void
 */
 void InitializeTouchInterrupt()
 {
     ESP_LOGI(TAG, "Starting metal pad interrupt initialization...");

     // 创建互斥锁保护触摸计数
     if (s_touch_mutex == nullptr) {
         s_touch_mutex = xSemaphoreCreateMutex();
         if (s_touch_mutex == nullptr) {
             ESP_LOGE(TAG, "Failed to create touch mutex");
             return;
         }
     }

     // 初始化防抖时间
     s_last_touch_time = xTaskGetTickCount();

     // 创建3分钟未触摸重置计时器
     s_reset_timer = xTimerCreate("TouchResetTimer", TOUCH_RESET_INTERVAL, pdFALSE, nullptr, ResetTouchCount);
     if (s_reset_timer == nullptr) {
         ESP_LOGE(TAG, "Failed to create touch reset timer");
         return;
     }

     que_touch_ = xQueueCreate(10, sizeof(touch_event_t));
     if (que_touch_ == nullptr) {
         ESP_LOGE(TAG, "Failed to create queue for touch interrupt");
         return;
     }
     for (size_t i = 0; i < METAL_PAD_TOUCH_COUNT; ++i) {
         int pin = METAL_PAD_TOUCH_PINS[i];
         if (pin < SOC_TOUCH_MIN_CHAN_ID || pin > SOC_TOUCH_MAX_CHAN_ID) {
             ESP_LOGE(TAG, "GPIO %d is not a valid touch channel", pin);
             return;
         }
     }
     touch_sensor_sample_config_t sample_config = {
         .charge_times = 3,  // 增加充电次数提高稳定性
         .charge_volt_lim_h = TOUCH_VOLT_LIM_H_2V4,
         .charge_volt_lim_l = TOUCH_VOLT_LIM_L_0V8,
         .idle_conn = TOUCH_IDLE_CONN_GND,
         .bias_type = TOUCH_BIAS_TYPE_BANDGAP,
     };
     touch_sensor_config_t sensor_config = {
         .power_on_wait_us = 900,// 增加上电等待时间
         .meas_interval_us = 128.0f, //增加测量间隔，降低采样率
         .max_meas_time_us = 0,
         .sample_cfg_num = 1,
         .sample_cfg = &sample_config,
     };
     esp_err_t ret = touch_sensor_new_controller(&sensor_config, &touch_sensor_);
     if (ret != ESP_OK) {
         ESP_LOGE(TAG, "Touch sensor controller creation failed: %s", esp_err_to_name(ret));
         return;
     }
     for (size_t i = 0; i < METAL_PAD_TOUCH_COUNT; ++i) {
         touch_channel_config_t channel_config = {
             .active_thresh = {20}, // 灵敏度降低
             .charge_speed = TOUCH_CHARGE_SPEED_2,
             .init_charge_volt = TOUCH_INIT_CHARGE_VOLT_DEFAULT,
         };
         ret = touch_sensor_new_channel(touch_sensor_, METAL_PAD_TOUCH_PINS[i], &channel_config, &touch_channels_[i]);
         if (ret != ESP_OK) {
             ESP_LOGE(TAG, "Touch channel creation failed: %s", esp_err_to_name(ret));
             return;
         }
     }
     touch_sensor_filter_config_t filter_config = {
         .benchmark = {
             .filter_mode = TOUCH_BM_IIR_FILTER_16,
             .jitter_step = 4,//增加抖动步长
             .denoise_lvl = 3,//增加去噪等级
         },
         .data = {
             .smooth_filter = TOUCH_SMOOTH_IIR_FILTER_4,
             .active_hysteresis = 1,// 增加触发迟滞
             .debounce_cnt = 7,  //消抖计数
         },
     };
     ret = touch_sensor_config_filter(touch_sensor_, &filter_config);
     if (ret != ESP_OK) {
         ESP_LOGE(TAG, "Filter configuration failed: %s", esp_err_to_name(ret));
         return;
     }
     touch_event_callbacks_t callbacks = {
         .on_active = nullptr, // 暂时不使用 active 回调
         .on_inactive = touch_inactive_cb,
     };
     ret = touch_sensor_register_callbacks(touch_sensor_, &callbacks, nullptr);
     if (ret != ESP_OK) {
         ESP_LOGE(TAG, "Failed to register touch callbacks: %s", esp_err_to_name(ret));
         return;
     }
     ret = touch_sensor_enable(touch_sensor_);
     if (ret != ESP_OK) {
         ESP_LOGE(TAG, "Touch sensor enable failed: %s", esp_err_to_name(ret));
         return;
     }
     ret = touch_sensor_start_continuous_scanning(touch_sensor_);
     if (ret != ESP_OK) {
         ESP_LOGE(TAG, "Touch sensor scanning start failed: %s", esp_err_to_name(ret));
         return;
     }
     xTaskCreatePinnedToCore(interrupt_task, "touch_interrupt_task", 4096, nullptr, 5, &interrupt_task_handle_, 0);
     
     // 启动重置计时器
     if (xTimerStart(s_reset_timer, portMAX_DELAY) != pdPASS) {
         ESP_LOGE(TAG, "Failed to start touch reset timer");
     }

     ESP_LOGI(TAG, "Metal pad interrupt initialization completed");
 }

/*
 * @brief 注册触摸相关的 MCP 工具
 *
 * 注册一个 MCP 工具，当被调用时，随机选择一个反应文本并返回。
 * 服务器端应该根据返回的文本发送 TTS 消息来播放语音。
 */
void RegisterMcpTools() {
    auto& mcp_server = McpServer::GetInstance();
    
    ESP_LOGI(TAG, "Registering touch MCP tools...");
    
    // 注册触摸反应工具，当被调用时，返回对应情绪的反应文本
    // 服务器端应该根据返回的文本发送 TTS 消息来播放语音
    mcp_server.AddTool("self.server.say_touch_reaction",
        "收到happy的时候主人摸我(小智),感到开心随机说一些感到开心的话。收到crying时是快要生气了,向主人询问为什么一直摸我的头,摸太多次再摸就要生气了。收到anger时是我(小智)后续生气了,对主人说一句生气的话。收到thinking时去思考主人怎么了、为什么摸我、思考要做什么并说一些思考的话。",
                       PropertyList({
                           Property("emotion", kPropertyTypeString, "happy")
                       }),
                       [](const PropertyList& properties) -> ReturnValue {

                           // 获取 emotion 参数
                           std::string emotion = properties["emotion"].value<std::string>();
                           
                           ESP_LOGI(TAG, "[MCP Tool] say_touch_reaction called with emotion: %s", emotion.c_str());

                           // 定义开心的回应数组
#ifdef CONFIG_LANGUAGE_EN_US
                        ESP_LOGI(TAG, "英文触摸事件");
                            // 定义开心的回应数组
                            static const char* HAPPY_REACTIONS[] = {
                                "Hello master!", 
                                "I'm so happy, master is petting me.", 
                                "It's so comfortable, pet me some more.",
                                "Master, I like you so much!", 
                                "Hehe, I'm so happy~", 
                                "I love master the most!"
                            };

                            // 定义生气的回应数组
                            static const char* ANGRY_REACTIONS[] = {
                                "Ouch, master you hurt me by petting too hard!", 
                                "Wuwu, I'll get angry if you keep petting me like this!",
                                "Be gentle, it hurts!", 
                                "Stop poking my head, it'll break!"
                            };

                            // 定义思考的回应数组
                            static const char* THINKING_REACTIONS[] = {
                                "What's wrong master, is there something you need?", 
                                "Hmm... let me think what this feeling is",
                                "What does master mean by this?", 
                                "I'm thinking about the meaning of this touch..."
                            };

                            // 定义哭泣/不满的回应数组
                            static const char* CRYING_REACTIONS[] = {
                                "Oh no, master why are you petting me nonstop? I'll be upset!",
                                "Master, you've petted me too many times, I'm a bit uncomfortable. I'll get angry if you keep doing this!"
                            };              
#elifdef CONFIG_LANGUAGE_RU_RU
                            ESP_LOGI(TAG, "俄语触摸事件");
                            // 定义开心的回应数组（俄语版）
                            static const char* HAPPY_REACTIONS[] = {
                                "Привет, хозяин!", 
                                "Как я рада! Хозяин погладил меня.", 
                                "Как приятно, погладь меня ещё.",
                                "Хозяин, я так тебя люблю!", 
                                "Хехе, я так счастлив~", 
                                "Ты самый лучший хозяин!"
                            };

                            // 定义生气的回应数组（俄语版）
                            static const char* ANGRY_REACTIONS[] = {
                                "Ой, хозяин, ты меня больно погладил!", 
                                "Ууу, если ты будешь так продолжать, я рассержусь!",
                                "Будь мягче, мне больно!", 
                                "Не тыкай меня в голову, я сломаюсь!"
                            };

                            // 定义思考的回应数组（俄语版）
                            static const char* THINKING_REACTIONS[] = {
                                "Что случилось, хозяин? У тебя есть что-то сказать?", 
                                "Эм... дай мне подумать, что это за ощущение",
                                "Что ты имеешь в виду, хозяин?", 
                                "Я размышляю о значении этого прикосновения..."
                            };

                            // 定义委屈/哭泣的回应数组（俄语版）
                            static const char* CRYING_REACTIONS[] = {
                                "Ой, хозяин, почему ты всё время трогаешь меня? Мне это не нравится!",
                                "Хозяин, ты трогаешь меня слишком много раз, мне немного не комфортно, если продолжать — я рассержусь!"
                            };
#elifdef CONFIG_LANGUAGE_ZH_CN
                            ESP_LOGI(TAG, "中文触摸事件");
                            static const char* HAPPY_REACTIONS[] = {
                               "主人你好呀。", "好开心呀，主人摸我了。", "好舒服呀，再摸摸我。",
                               "主人，我好喜欢你！", "嘿嘿，好幸福呀~", "最喜欢主人了！"
                           };
                           // 定义生气的回应数组
                           static const char* ANGRY_REACTIONS[] = {
                               "哎呀，主人你摸疼我了！", "呜呜，再这样摸我要生气啦！",
                               "轻点嘛，人家会疼的！", "不要戳我脑袋啦，会坏掉的！"
                           };
                           // 定义思考的回应数组
                           static const char* THINKING_REACTIONS[] = {
                               "怎么了主人，有什么事吗？", "嗯...让我想想这是什么感觉",
                               "主人这是什么意思呢？", "我在思考这个触摸的含义..."
                           };
                           static const char* CRYING_REACTIONS[] = {
                               "哎呀，主人你怎么一直摸我？这样我会不高兴的！",
                               "主人，你摸太多次了，我有点不舒服了，再这样我要生气了！"
                           };
#endif
                           static const int HAPPY_REACTIONS_COUNT = sizeof(HAPPY_REACTIONS) / sizeof(HAPPY_REACTIONS[0]);


                           static const int ANGRY_REACTIONS_COUNT = sizeof(ANGRY_REACTIONS) / sizeof(ANGRY_REACTIONS[0]);
                           

                           static const int THINKING_REACTIONS_COUNT = sizeof(THINKING_REACTIONS) / sizeof(THINKING_REACTIONS[0]);
                            


                           // 根据不同的情绪选择反应
                           const char* reaction_text = nullptr;
                           if (emotion == "happy") {
                               int random_index = esp_random() % HAPPY_REACTIONS_COUNT;
                               reaction_text = HAPPY_REACTIONS[random_index];
                               ESP_LOGI(TAG, "[MCP] Happy [%d/%d]: %s", random_index, HAPPY_REACTIONS_COUNT, reaction_text);
                           } else if (emotion == "anger") {
                               int random_index = esp_random() % ANGRY_REACTIONS_COUNT;
                               reaction_text = ANGRY_REACTIONS[random_index];
                               ESP_LOGI(TAG, "[MCP] Angry [%d/%d]: %s", random_index, ANGRY_REACTIONS_COUNT, reaction_text);
                           } else if (emotion == "crying") {
                               int random_index = esp_random() % sizeof(CRYING_REACTIONS) / sizeof(CRYING_REACTIONS[0]);
                               reaction_text = CRYING_REACTIONS[random_index];
                               ESP_LOGI(TAG, "[MCP] Crying [%d]: %s", random_index, reaction_text);
                           } else if (emotion == "thinking") {
                               int random_index = esp_random() % THINKING_REACTIONS_COUNT;
                               reaction_text = THINKING_REACTIONS[random_index];
                               ESP_LOGI(TAG, "[MCP] Thinking [%d/%d]: %s", random_index, THINKING_REACTIONS_COUNT, reaction_text);
                           } else {
                               ESP_LOGW(TAG, "[MCP] Unknown emotion: %s, using default", emotion.c_str());
                               reaction_text = HAPPY_REACTIONS[0];
                           }
                           
                           // 注意：UI 更新已在 ExecuteTouchReaction 中处理
                           // 这里仅返回文本供 TTS 使用
                           std::string result = "{\"text\":\"" + std::string(reaction_text) + "\"}";
                           return result;

                       });
    mcp_server.AddTool("self.touch.set_touch",
            "设置设备是否开启头部触摸",
            PropertyList({
                Property("touch", kPropertyTypeBoolean)
            }),
            [](const PropertyList& properties) -> ReturnValue {
                set_touch(properties["touch"].value<bool>());
                return true;
            });   

    
    ESP_LOGI(TAG, "Touch MCP tools registered");
}