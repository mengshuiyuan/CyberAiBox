#include "wifi_board.h"
#include "dual_network_board.h"
#include "codecs/box_audio_codec.h"
#include "codecs/no_audio_codec.h"
#include "display/lcd_display.h"
#include "display/emote_display.h"
#include "application.h"
#include "qmi8658.h"//姿态传感器
#include "button.h"
#include "config.h"
#include <esp_lcd_gc9d01.h>
#include <esp_lcd_gc9a01.h>
#include <esp_log.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <wifi_station.h>
#include <font_emoji.h>
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include <driver/gpio.h>
//触摸
#include "touch.h"
#include <cmath>

//电源
#include "bq27220/bq27220.h"
#include <driver/temperature_sensor.h>

//RGB灯
#include "ws2812b_controller.h"
//#include "led.h"

#define TAG "CyberAiDualScreen"

#include "assets/lang_config.h"

//电量部分
static const parameter_cedv_t default_cedv = {
    .full_charge_cap = 1200,    /* 额定容量1200mAh */
    .design_cap = 1200,         /* 设计容量1200mAh */
    .reserve_cap = 60,          /* 储备容量，约5%的额定容量 */
    .near_full = 1140,          /* 接近充满阈值，95%容量 */
    .self_discharge_rate = 8,   /* 自放电率：8 × 0.0025% = 0.02%/天 */
    
    /* EDV参数 - 基于放电终止电压3.2V，但调整上限为4.1V */
    .EDV0 = 3200,               /* 0% SOC - 完全放空电压 */
    .EDV1 = 3300,               /* 3% SOC */
    .EDV2 = 3400,               /* 电池低电量警告电压 */
    
    /* 电化学参数调整 */
    .EMF = 4050,                /* 空载电压，调整为略低于4.1V充满电压 */
    .C0 = 1400,                 /* 稍小的容量相关EDV调整因子（电压范围变小） */
    .R0 = 55,                   /* 稍大的阻抗调整因子（电压窗口变小） */
    .T0 = 100,                  /* 温度阻抗变化因子 */
    .R1 = 28,                   /* 稍大的容量阻抗变化因子 */
    .TC = 125,                  /* 冷温度阻抗调整 */
    .C1 = 35,                   /* EDV0时保留容量稍大为35mAh */
    
    /* DOD电压曲线 - 基于3.2V-4.1V范围重新调整 */
    .DOD0 = 4100,               /* 0% DOD - 充满4.1V */
    .DOD10 = 4030,              /* 10% DOD - 4.03V */
    .DOD20 = 3970,              /* 20% DOD - 3.97V */
    .DOD30 = 3910,              /* 30% DOD - 3.91V */
    .DOD40 = 3850,              /* 40% DOD - 3.85V */
    .DOD50 = 3800,              /* 50% DOD - 3.80V */
    .DOD60 = 3750,              /* 60% DOD - 3.75V */
    .DOD70 = 3700,              /* 70% DOD - 3.70V */
    .DOD80 = 3620,              /* 80% DOD - 3.62V */
    .DOD90 = 3520,              /* 90% DOD - 3.52V */
    .DOD100 = 3200              /* 100% DOD - 放空3.2V */
};

static const gauging_config_t default_config = {
    .CCT = 1,        /* 使用充满容量百分比 */
    .CSYNC = 1,      /* 启用容量同步 */
    .EDV_CMP = 1,    /* 启用EDV补偿 */
    .SC = 1,         /* 启用短路检测 */
    .FIXED_EDV0 = 0, /* 不固定EDV0 */
    .FCC_LIM = 1,    /* 启用充满容量限制 */
    .FC_FOR_VDQ = 0, /* 不为VDQ使用充满电压 */
    .IGNORE_SD = 0,  /* 不忽略自放电 */
    .SME0 = 0        /* 禁用特定模式 */
};

temperature_sensor_handle_t temp_sensor = NULL;

enum ShakeEventType {
        SHAKE_DETECTED,     // 摇晃
        IMPACT_DETECTED,    // 撞击/跌落
        ORIENTATION_CHANGED // 方向改变
    };


class CyberAiDualScreen : public DualNetworkBoard {
private:
    i2c_master_bus_handle_t codec_i2c_bus_;
    Button boot_button_;
    //Button touch_button_;//将头部触摸设置成按钮
    Display* display_;
    Display* dual_display_[2];//双屏异显
    adc_oneshot_unit_handle_t adc1_handle;
    adc_cali_handle_t adc1_cali_handle;
    bool do_calibration = false;

    i2c_master_bus_handle_t imu_i2c_bus_ = nullptr;
    qmi8658_dev_t imu_dev_ = {};
    qmi8658_data_t data;
    bool imu_initialized_ = false;

    // 摇晃检测相关
    TaskHandle_t imu_task_handle_ = nullptr;
    QueueHandle_t imu_event_queue_ = nullptr;
    
    // 加速度校准值
    float accel_offset_x_ = 0;
    float accel_offset_y_ = 0;
    float accel_offset_z_ = 0;
    
    // 状态标志
    bool is_shaking_ = false;
    bool is_calibrated_ = false;
    
    // 检测参数
    struct ShakeDetectionParams {
        float shake_threshold = 30.0f;      // 摇晃阈值 (m/s²)
        float impact_threshold = 45.0f;     // 撞击阈值 (m/s²)
        int window_size = 10;               // 滑动窗口大小
        int shake_duration_ms = 1000;        // 最短摇晃持续时间
        int cooldown_ms = 5000;             // 检测冷却时间
    } shake_params_;

    // 防抖相关
    TickType_t last_shake_event_time_ = 0;
    TickType_t last_impact_event_time_ = 0;
    const TickType_t SHAKE_DEBOUNCE_MS = pdMS_TO_TICKS(5000);    // 摇晃5秒防抖
    const TickType_t IMPACT_DEBOUNCE_MS = pdMS_TO_TICKS(3000);   // 撞击3秒防抖
    
    // 添加一个过滤队列来防止快速连续事件
    QueueHandle_t filtered_event_queue_ = nullptr;

    bq27220_handle_t bq27220 = NULL;
    i2c_bus_handle_t i2c_bus = NULL; //bmi270使用
    

    void InitializeI2c() {
        // i2c_master_bus_config_t i2c_bus_cfg = {
        //     .i2c_port = I2C_NUM_0,
        //     .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
        //     .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
        //     .clk_source = I2C_CLK_SRC_DEFAULT,
        //     .glitch_ignore_cnt = 7,
        //     .intr_priority = 0,
        //     .trans_queue_depth = 0,
        //     .flags = {
        //         .enable_internal_pullup = 1,
        //     },
        // };
        // ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &codec_i2c_bus_));

        i2c_config_t i2c_cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
        .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master = {
            .clk_speed = 400 * 1000
        },
        .clk_flags = 0  // 或者 I2C_SCLK_SRC_FLAG_AWARE_DFS | I2C_SCLK_SRC_FLAG_LIGHT_SLEEP
        };
        i2c_bus = i2c_bus_create(I2C_NUM_0, &i2c_cfg);
        codec_i2c_bus_ = i2c_bus_get_internal_bus_handle(i2c_bus);

        vTaskDelay(pdMS_TO_TICKS(50));

        temperature_sensor_config_t temp_sensor_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
        ESP_ERROR_CHECK(temperature_sensor_install(&temp_sensor_config, &temp_sensor));
        ESP_ERROR_CHECK(temperature_sensor_enable(temp_sensor));
        ESP_LOGI(TAG, "I2C initialized"); 
    }

    // void InitializeChargeGPIO() {
    //     gpio_config_t charge_detect_config = {
    //         .pin_bit_mask = (1ULL << CHARGE_DETECT_PIN),
    //         .mode = GPIO_MODE_INPUT,
    //         .pull_up_en = GPIO_PULLUP_ENABLE,
    //         .pull_down_en = GPIO_PULLDOWN_DISABLE,
    //         .intr_type = GPIO_INTR_DISABLE
    //     };
    //     ESP_ERROR_CHECK(gpio_config(&charge_detect_config));
    //     gpio_config_t charge_stdby_config = {
    //         .pin_bit_mask = (1ULL << CHARGE_STDBY_PIN),
    //         .mode = GPIO_MODE_INPUT,
    //         .pull_up_en = GPIO_PULLUP_ENABLE,
    //         .pull_down_en = GPIO_PULLDOWN_DISABLE,
    //         .intr_type = GPIO_INTR_DISABLE
    //     };
    //     ESP_ERROR_CHECK(gpio_config(&charge_stdby_config));
    // }

    void InitializeADC() {
        adc_oneshot_unit_init_cfg_t init_config1 = {
            .unit_id = ADC_UNIT_1
        };
        ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

        adc_oneshot_chan_cfg_t chan_config = {
            .atten = ADC_ATTEN,
            .bitwidth = ADC_WIDTH,
        };
        ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, VBAT_ADC_CHANNEL, &chan_config));

        adc_cali_handle_t handle = NULL;
        esp_err_t ret = ESP_FAIL;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = ADC_UNIT_1,
            .atten = ADC_ATTEN,
            .bitwidth = ADC_WIDTH,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            do_calibration = true;
            adc1_cali_handle = handle;
            ESP_LOGI(TAG, "ADC Curve Fitting calibration succeeded");
        }
#endif // ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_SPI_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_SPI_SCLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeButtons() {
        // gpio_config_t boot_config = {
        //     .pin_bit_mask = (1ULL << BOOT_BUTTON_GPIO),
        //     .mode = GPIO_MODE_INPUT,
        //     .pull_up_en = GPIO_PULLUP_DISABLE,
        //     .pull_down_en = GPIO_PULLDOWN_DISABLE,
        //     .intr_type = GPIO_INTR_DISABLE
        // };
        // ESP_ERROR_CHECK(gpio_config(&boot_config));
        //gpio_set_level(BOOT_BUTTON_GPIO , 1);
        // gpio_config_t touch_config = {
        //     .pin_bit_mask = (1ULL << TOUCH_BUTTON),
        //     .mode = GPIO_MODE_INPUT,
        //     .pull_up_en = GPIO_PULLUP_DISABLE,
        //     .pull_down_en = GPIO_PULLDOWN_DISABLE,
        //     .intr_type = GPIO_INTR_DISABLE
        // };
        // ESP_ERROR_CHECK(gpio_config(&touch_config));
        // boot_button_.OnClick([this]() {
        //     ESP_LOGI(TAG, "Boot button clicked");
        //     auto& app = Application::GetInstance();
        //     app.ToggleChatState();
        // });
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            // if (GetNetworkType() == NetworkType::WIFI) {
            //     if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
            //         // cast to WifiBoard
            //         auto& wifi_board = static_cast<WifiBoard&>(GetCurrentBoard());
            //         wifi_board.ResetWifiConfiguration();
            //     }
            // }
            if (app.GetDeviceState() == kDeviceStateStarting) {
                    // cast to WifiBoard
                    auto& wifi_board = static_cast<WifiBoard&>(GetCurrentBoard());
                    wifi_board.ResetWifiConfiguration();
            }
            if(GetMusic()->MusicPlaying())
            {
                GetMusic()->StopStreaming();
            }
            app.WakeWordInvoke(Lang::Strings::HELLO_ARE_YOU_HERE);
            app.ToggleChatState();
        });
        boot_button_.OnDoubleClick([this]() {
            auto& app = Application::GetInstance();
            if (GetNetworkType() == NetworkType::WIFI) {
                if (app.GetDeviceState() == kDeviceStateIdle || app.GetDeviceState() == kDeviceStateStarting) {
                    // cast to WifiBoard
                    auto& wifi_board = static_cast<WifiBoard&>(GetCurrentBoard());
                    wifi_board.ResetWifiConfiguration();
                }
            }

        });
        // touch_button_.OnClick([this]() {
        //     ESP_LOGI(TAG, "Touch button clicked");
        //     auto& app = Application::GetInstance();
        //     app.TriggerWakeWord("嘿，我在摸你的头");
        // });
        // if (GetNetworkType() == NetworkType::ML307) {
        //     auto& app = Application::GetInstance();
        //     if (app.GetDeviceState() == kDeviceStateStarting || app.GetDeviceState() == kDeviceStateWifiConfiguring) {
        //         SwitchNetworkType();
        //     }
        // }
    }

    void InitializeDisplay() {
        gpio_config_t charge_detect_config = {
            .pin_bit_mask = (1ULL << DISPLAY_RIGHT_SPI_CS_PIN),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        ESP_ERROR_CHECK(gpio_config(&charge_detect_config));
        gpio_set_level(DISPLAY_RIGHT_SPI_CS_PIN , 0);
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        
        ESP_LOGI(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_LEFT_SPI_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_SPI_DC_PIN;
        io_config.spi_mode = 0;
        io_config.pclk_hz = 60 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config, &panel_io));

        ESP_LOGI(TAG, "Install GC9D01 panel driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_SPI_RESET_PIN;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;//LCD_RGB_ELEMENT_ORDER_BGR   LCD_RGB_ELEMENT_ORDER_BGR
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(panel_io, &panel_config, &panel));
        
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, true));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y));
        ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

        display_ = new SpiLcdDisplay(panel_io, panel,
                        DISPLAY_WIDTH, DISPLAY_HEIGHT, 
                        DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, 
                        DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }
    void IntializeDualScreen()
    {
            // Panel left screen
        esp_lcd_panel_io_handle_t io_handle1 = NULL;
        esp_lcd_panel_io_spi_config_t io_config1 = GC9D01_PANEL_IO_SPI_CONFIG(DISPLAY_LEFT_SPI_CS_PIN, DISPLAY_SPI_DC_PIN, NULL, NULL);
        io_config1.spi_mode = 0;
        io_config1.pclk_hz = LCD_SPI_PCLK_HZ;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config1, &io_handle1));

        esp_lcd_panel_handle_t panel_handle1 = NULL;
        esp_lcd_panel_dev_config_t panel_config1 = {};
        panel_config1.reset_gpio_num = DISPLAY_SPI_RESET_PIN; // Uses shared RST
        panel_config1.rgb_endian = LCD_RGB_ENDIAN_RGB;
        panel_config1.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
        panel_config1.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_gc9d01(io_handle1, &panel_config1, &panel_handle1));

        // Panel right screen
        esp_lcd_panel_io_handle_t io_handle2 = NULL;
        esp_lcd_panel_io_spi_config_t io_config2 = GC9D01_PANEL_IO_SPI_CONFIG(DISPLAY_RIGHT_SPI_CS_PIN, DISPLAY_SPI_DC_PIN, NULL, NULL);
        io_config2.spi_mode = 0;
        io_config2.pclk_hz = LCD_SPI_PCLK_HZ;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config2, &io_handle2));

        esp_lcd_panel_handle_t panel_handle2 = NULL;
        esp_lcd_panel_dev_config_t panel_config2 = {};
        panel_config2.reset_gpio_num = GPIO_NUM_NC; // Don't reset shared pin again
        panel_config2.rgb_endian = LCD_RGB_ENDIAN_RGB;
        panel_config2.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
        panel_config2.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_gc9d01(io_handle2, &panel_config2, &panel_handle2));

        // Init Panel 1
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle1)); // This resets both if shared
        vTaskDelay(pdMS_TO_TICKS(20));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle1));
        vTaskDelay(pdMS_TO_TICKS(20));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle1, true));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle1, false, false));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle1, true));

        // Init Panel 2
        // Skip reset for panel 2 as it was reset by panel 1
        vTaskDelay(pdMS_TO_TICKS(20));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle2));
        vTaskDelay(pdMS_TO_TICKS(20));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle2, true));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle2, false, false));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle2, true));

        // Backlights
        gpio_config_t bl_cfg = {};
        bl_cfg.pin_bit_mask = BIT64(DISPLAY_LEFT_BACKLIGHT_PIN) | BIT64(DISPLAY_RIGHT_BACKLIGHT_PIN);
        bl_cfg.mode = GPIO_MODE_OUTPUT;
        bl_cfg.pull_up_en = GPIO_PULLUP_DISABLE;
        bl_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        bl_cfg.intr_type = GPIO_INTR_DISABLE;
        ESP_ERROR_CHECK(gpio_config(&bl_cfg));
        ESP_ERROR_CHECK(gpio_set_level(DISPLAY_LEFT_BACKLIGHT_PIN, 1));
        ESP_ERROR_CHECK(gpio_set_level(DISPLAY_RIGHT_BACKLIGHT_PIN, 1));

        dual_display_[0] = new SpiLcdDisplay(io_handle1, panel_handle1,
                        DISPLAY_WIDTH, DISPLAY_HEIGHT, 
                        DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, 
                        DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
        dual_display_[1] = new SpiLcdDisplay(io_handle2, panel_handle2,
                        DISPLAY_WIDTH, DISPLAY_HEIGHT, 
                        DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, 
                        DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);


    }
    void Initialize4G(){
        gpio_config_t charge_detect_config = {
            .pin_bit_mask = (1ULL << EN_4G),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        ESP_ERROR_CHECK(gpio_config(&charge_detect_config));
        gpio_set_level(EN_4G , 0);//4G高电平开机
        ESP_LOGI(TAG,"使能4G 14引脚电平1: %d", gpio_get_level(GPIO_NUM_14));
        vTaskDelay(pdMS_TO_TICKS(100)); 
        gpio_set_level(EN_4G , 1); 
        ESP_LOGI(TAG,"使能4G 14引脚电平2: %d", gpio_get_level(GPIO_NUM_14));
        vTaskDelay(pdMS_TO_TICKS(100)); 

        if (GetNetworkType() != NetworkType::WIFI) {
            auto& wifi_board = static_cast<WifiBoard&>(GetCurrentBoard());
            SwitchNetworkType();
        }

        // gpio_config_t config = {
        //     .pin_bit_mask = (1ULL << GPIO_NUM_11),
        //     .mode = GPIO_MODE_OUTPUT,
        //     .pull_up_en = GPIO_PULLUP_DISABLE,
        //     .pull_down_en = GPIO_PULLDOWN_DISABLE,
        //     .intr_type = GPIO_INTR_DISABLE
        // };
        // ESP_ERROR_CHECK(gpio_config(&config));
        // gpio_set_level(GPIO_NUM_11 , 0);//4G高电平开机
        // ESP_LOGI(TAG,"使能11引脚电平1: %d", gpio_get_level(GPIO_NUM_11));
        // vTaskDelay(pdMS_TO_TICKS(100)); 
        // gpio_set_level(GPIO_NUM_11 , 1);
        // ESP_LOGI(TAG,"使能11引脚电平2: %d", gpio_get_level(GPIO_NUM_11));    
    }
    //姿态传感器
    void InitializeImu() {
        // IMU 与音频编解码器共用 I2C_NUM_0，引脚在 config.h 中固定为 GPIO4/5
        if (!codec_i2c_bus_) {
            ESP_LOGE(TAG, "Codec I2C bus not ready, skip IMU init");
            return;
        }
        imu_i2c_bus_ = codec_i2c_bus_;

        ScanImuBus();

        esp_err_t err = qmi8658_init(&imu_dev_, imu_i2c_bus_, QMI8658_ADDRESS_LOW);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to init QMI8658: %s", esp_err_to_name(err));
            return;
        }

        //qmi8658_set_accel_unit_mps2(&imu_dev_, true);
        imu_initialized_ = true;
        ESP_LOGI(TAG, "QMI8658 ready on SDA GPIO4 / SCL GPIO5");
        qmi8658_set_accel_range(&imu_dev_, QMI8658_ACCEL_RANGE_8G);
        qmi8658_set_accel_odr(&imu_dev_, QMI8658_ACCEL_ODR_1000HZ);
        qmi8658_set_gyro_range(&imu_dev_, QMI8658_GYRO_RANGE_512DPS);
        qmi8658_set_gyro_odr(&imu_dev_, QMI8658_GYRO_ODR_1000HZ);
        qmi8658_set_accel_unit_mps2(&imu_dev_, true);
        qmi8658_set_gyro_unit_rads(&imu_dev_, true);
    }

    void ScanImuBus() {
        if (!imu_i2c_bus_) {
            return;
        }
        ESP_LOGI(TAG, "Scanning IMU I2C bus for active devices...");
        bool device_found = false;
        for (uint8_t addr = 0x08; addr < 0x78; ++addr) {
            esp_err_t ret = i2c_master_probe(imu_i2c_bus_, addr, 50);
            if (ret == ESP_OK) {
                device_found = true;
                ESP_LOGI(TAG, "I2C device ACK at 0x%02X", addr);
            }
        }
        if (!device_found) {
            ESP_LOGW(TAG, "No I2C devices responded on IMU bus");
        }
    }
    // 在 InitializeImu() 方法后添加
    void InitializeShakeDetection() {
        if (!imu_initialized_) {
            ESP_LOGW(TAG, "IMU not initialized, skip shake detection init");
            return;
        }
        
        // 创建原始事件队列
        imu_event_queue_ = xQueueCreate(20, sizeof(ShakeEventType));
        
        // 创建过滤后的事件队列（应用层从这个队列读取）
        filtered_event_queue_ = xQueueCreate(10, sizeof(ShakeEventType));
        
        if (!imu_event_queue_ || !filtered_event_queue_) {
            ESP_LOGE(TAG, "Failed to create event queues");
            return;
        }
        
        // 启动IMU监控任务
        xTaskCreate(
            ImuMonitoringTask,
            "imu_monitor",
            4096,
            this,
            5,
            &imu_task_handle_
        );
        
        // 启动事件过滤器任务
        xTaskCreate(
            EventFilterTask,
            "event_filter",
            2048,
            this,
            4,
            nullptr
        );
    }
    // 事件过滤器任务
    static void EventFilterTask(void* arg) {
        CyberAiDualScreen* self = static_cast<CyberAiDualScreen*>(arg);
        
        ESP_LOGI(TAG, "Event filter task started");
        
        while (true) {
            ShakeEventType event;
            
            // 从原始队列接收事件
            if (xQueueReceive(self->imu_event_queue_, &event, portMAX_DELAY)) {
                bool should_pass = false;
                TickType_t now = xTaskGetTickCount();
                
                switch (event) {
                    case SHAKE_DETECTED:
                        if (now - self->last_shake_event_time_ >= self->SHAKE_DEBOUNCE_MS) {
                            self->last_shake_event_time_ = now;
                            should_pass = true;
                            
                            // 记录详细的防抖信息
                            ESP_LOGI(TAG, "Shake event passed filter (debounce: %d ms)", 
                                    self->SHAKE_DEBOUNCE_MS * portTICK_PERIOD_MS);
                        } else {
                            TickType_t remaining = (self->last_shake_event_time_ + 
                                                   self->SHAKE_DEBOUNCE_MS - now) * 
                                                   portTICK_PERIOD_MS;
                            ESP_LOGD(TAG, "Shake filtered out, %d ms remaining", remaining);
                        }
                        break;
                        
                    case IMPACT_DETECTED:
                        if (now - self->last_impact_event_time_ >= self->IMPACT_DEBOUNCE_MS) {
                            self->last_impact_event_time_ = now;
                            should_pass = true;
                        }
                        break;
                        
                    case ORIENTATION_CHANGED:
                        should_pass = true;  // 方向改变不过滤
                        break;
                }
                
                // 将过滤后的事件发送到应用队列
                if (should_pass && self->filtered_event_queue_) {
                    xQueueSend(self->filtered_event_queue_, &event, 0);
                }
            }
        }
    }
    

    // IMU监控任务
    static void ImuMonitoringTask(void* arg) {
        CyberAiDualScreen* self = static_cast<CyberAiDualScreen*>(arg);
        
        // 滑动窗口用于平滑
        std::vector<float> accel_magnitudes;
        accel_magnitudes.reserve(self->shake_params_.window_size);
        
        // 时间跟踪
        TickType_t last_shake_time = 0;
        TickType_t last_detection_time = 0;
        
        ESP_LOGI(TAG, "IMU monitoring task started");
        
        while (true) {
            float ax, ay, az;
            
            if (self->ReadAccelerometer(ax, ay, az)) {
                // 应用校准
                if (self->is_calibrated_) {
                    ax -= self->accel_offset_x_;
                    ay -= self->accel_offset_y_;
                    az -= self->accel_offset_z_;
                }
                
                // 计算加速度幅值（去除重力）
                float magnitude = self->CalculateAccelerationMagnitude(ax, ay, az);
                
                // 添加到滑动窗口
                if (accel_magnitudes.size() >= self->shake_params_.window_size) {
                    accel_magnitudes.erase(accel_magnitudes.begin());
                }
                accel_magnitudes.push_back(magnitude);
                
                // 计算窗口内的平均值和方差
                if (accel_magnitudes.size() >= self->shake_params_.window_size) {
                    float sum = 0;
                    for (float val : accel_magnitudes) {
                        sum += val;
                    }
                    float avg = sum / accel_magnitudes.size();
                    
                    float variance = 0;
                    for (float val : accel_magnitudes) {
                        variance += (val - avg) * (val - avg);
                    }
                    variance /= accel_magnitudes.size();
                    
                    // 检测摇晃（基于方差和阈值）
                    TickType_t now = xTaskGetTickCount();
                    TickType_t time_since_last = now - last_detection_time;
                    ESP_LOGD(TAG, "Shake detected: avg=%.2f, var=%.2f", avg, variance);
                    if (variance > 6.0f &&  // 变化大
                        avg > 12.0f &&       // 平均加速度较大
                        time_since_last > pdMS_TO_TICKS(self->shake_params_.cooldown_ms)) {
                        
                        // 检查是否是持续的摇晃
                        if (self->DetectShake(ax, ay, az)) {
                            last_shake_time = now;
                            self->NotifyShakeEvent(SHAKE_DETECTED);
                            last_detection_time = now;
                            self->is_shaking_ = true;
                            
                            // 等待摇晃结束
                            vTaskDelay(pdMS_TO_TICKS(self->shake_params_.shake_duration_ms));
                            self->is_shaking_ = false;
                        }
                    }
                    
                    // 检测撞击/跌落
                    if (magnitude > self->shake_params_.impact_threshold &&
                        time_since_last > pdMS_TO_TICKS(self->shake_params_.cooldown_ms)) {
                        self->NotifyShakeEvent(IMPACT_DETECTED);
                        last_detection_time = now;
                    }
                }
            }
            
            vTaskDelay(pdMS_TO_TICKS(200));  // 50Hz采样率
        }
    }

    float CalculateAccelerationMagnitude(float x, float y, float z) {
        // 减去重力后的净加速度
        float net_x = x;
        float net_y = y;
        float net_z = z - 9.807f;  // 减去重力
        
        return sqrt(net_x*net_x + net_y*net_y + net_z*net_z);
    }

    bool DetectShake(float ax, float ay, float az) {
        static std::vector<float> history_x, history_y, history_z;
        static const int history_size = 5;
        
        // 保存历史数据
        if (history_x.size() >= history_size) {
            history_x.erase(history_x.begin());
            history_y.erase(history_y.begin());
            history_z.erase(history_z.begin());
        }
        
        history_x.push_back(ax);
        history_y.push_back(ay);
        history_z.push_back(az);
        
        if (history_x.size() < history_size) {
            return false;
        }
        
        // 计算变化率
        float max_change = 0;
        for (int i = 1; i < history_size; i++) {
            float change_x = abs(history_x[i] - history_x[i-1]);
            float change_y = abs(history_y[i] - history_y[i-1]);
            float change_z = abs(history_z[i] - history_z[i-1]);
            
            max_change = std::max(max_change, std::max(change_x, std::max(change_y, change_z)));
        }
        
        // 阈值检测
        return max_change > shake_params_.shake_threshold / 2.0f;
    }
    // 应用层获取事件（带超时）
    bool GetShakeEvent(ShakeEventType* event, TickType_t timeout_ms = 0) {
        if (!filtered_event_queue_) return false;
        
        return xQueueReceive(filtered_event_queue_, event, 
                           pdMS_TO_TICKS(timeout_ms));
    }

    void NotifyShakeEvent(ShakeEventType type) {
        // 发送事件到队列
        if (imu_event_queue_) {
            xQueueSend(imu_event_queue_, &type, 0);
        }
        
        // 同时触发应用层回调
        auto& app = Application::GetInstance();
        auto& board = Board::GetInstance();
        auto display = board.GetDisplay();
        switch (type) {
            case SHAKE_DETECTED:
                ESP_LOGI(TAG, "设备摇晃检测!");
                if(app.GetDeviceState() == kDeviceStateListening||app.GetDeviceState() == kDeviceStateSpeaking) 
                {
                    app.TriggerWakeWord(Lang::Strings::SHAKE);
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    display->SetEmotion("confused");

                }
                else if(app.GetDeviceState() == kDeviceStateIdle)
                {
                    app.TriggerWakeWord(Lang::Strings::SHAKE_AWAKE);
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    display->SetEmotion("confused");
                }
                break;
            case IMPACT_DETECTED:
                ESP_LOGI(TAG, "设备撞击/跌落检测!");
                
                break;
            case ORIENTATION_CHANGED:
                ESP_LOGI(TAG, "设备方向改变!");
                
                break;
        }
    }
    //初始化bq272220芯片(电量监控芯片)
void Initializebq27220()
{
    if (codec_i2c_bus_ == NULL) {
        ESP_LOGE("BQ27220", "I2C bus is NULL, initialization failed");
        return;
    }
    
    bq27220_config_t bq27220_cfg = {
        .i2c_bus = i2c_bus,
        .cfg = &default_config,
        .cedv = &default_cedv,
    };
    
    bq27220 = bq27220_create(&bq27220_cfg);
    
    if (bq27220) {
        ESP_LOGI("BQ27220", "BQ27220 initialized successfully");
    } else {
        if(!bq27220_create(&bq27220_cfg))
        {
            ESP_LOGE("BQ27220", "BQ27220 initialization failed");
        }
    }
    
}
void InitializeRGBLight()
{
        static WS2812BController ambient_light(RGB_GPIO, 4);  // 4个WS2812B LED
        // // 启用自动状态控制
        ambient_light.EnableAutoStateControl(true);
        //xTaskCreate(led_task, "led_task", 2048, NULL, 5, NULL);
}

public:
    CyberAiDualScreen() : DualNetworkBoard(ML307_TX_PIN, ML307_RX_PIN, GPIO_NUM_NC,(int32_t)NetworkType::WIFI),//GPIO_NUM_NC
    boot_button_(BOOT_BUTTON_GPIO){//,touch_button_(TOUCH_BUTTON)
        InitializeI2c();
        Initialize4G();
        
        //InitializeChargeGPIO();//初始化充电检测
        //InitializeADC();
        InitializeSpi();
        InitializeDisplay();
        InitializeButtons();
        Initializebq27220();
        
        InitializeImu();
        // 在IMU初始化后添加
        InitializeShakeDetection();
        
        InitializeTouchInterrupt();
        RegisterMcpTools();
        InitializeRGBLight();

        //SwitchNetworkType();
        // Initialize left backlight
        if (DISPLAY_LEFT_BACKLIGHT_PIN != GPIO_NUM_NC) {
            GetBacklight()->RestoreBrightness();
        }
        
        // Initialize right backlight if available
        if (DISPLAY_RIGHT_BACKLIGHT_PIN != GPIO_NUM_NC) {
            // Create a temporary backlight instance for the right screen
            static PwmBacklight right_backlight(DISPLAY_RIGHT_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
            right_backlight.RestoreBrightness();
        }
    }

    virtual AudioCodec* GetAudioCodec() override
    {
        static BoxAudioCodec audio_codec(
            codec_i2c_bus_,
            AUDIO_INPUT_SAMPLE_RATE,
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK,
            AUDIO_I2S_GPIO_BCLK,
            AUDIO_I2S_GPIO_WS,
            AUDIO_I2S_GPIO_DOUT,
            AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN,
            AUDIO_CODEC_ES8311_ADDR,
            AUDIO_CODEC_ES7210_ADDR,
            AUDIO_INPUT_REFERENCE);
        return &audio_codec;
    }
    // virtual AudioCodec* GetAudioCodec() override {
    //     static NoAudioCodecDuplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
    //         GPIO_NUM_NC, GPIO_NUM_NC, GPIO_NUM_NC, GPIO_NUM_NC);
    //     return &audio_codec;
    // }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual bool ReadAccelerometer(float& x, float& y, float& z) override {
        // if (!imu_initialized_) {
        //     return false;
        // }
        // if (qmi8658_read_accel(&imu_dev_, &x, &y, &z) == ESP_OK) {
        //     return true;
        // }
        // return false;
        bool ready;
        esp_err_t ret = qmi8658_is_data_ready(&imu_dev_, &ready);
        if (ret == ESP_OK && ready) {
            ret = qmi8658_read_sensor_data(&imu_dev_, &data);
            if (ret == ESP_OK) {
                // ESP_LOGI(TAG, "Accel: X=%.4f m/s², Y=%.4f m/s², Z=%.4f m/s²",
                //          data.accelX, data.accelY, data.accelZ);
                // ESP_LOGI(TAG, "Gyro:  X=%.4f rad/s, Y=%.4f rad/s, Z=%.4f rad/s",
                //          data.gyroX, data.gyroY, data.gyroZ);
                // ESP_LOGI(TAG, "Temp:  %.2f °C, Timestamp: %lu",
                //          data.temperature, data.timestamp);
                // ESP_LOGI(TAG, "----------------------------------------");
                x = data.accelX;
                y = data.accelY;
                z = data.accelZ;
                return true;
            } else {
                ESP_LOGE(TAG, "Failed to read sensor data (error: %d)", ret);
                return false;
            }
        } else {
            ESP_LOGE(TAG, "Data not ready or error reading status (error: %d)", ret);
            return false;
        }
    }
    
    
    bool GetBatteryLevel(int &level, bool& charging, bool& discharging)
    {
        level = bq27220_get_state_of_charge(bq27220);
        battery_status_t status = {};
        bq27220_get_battery_status(bq27220, &status);
        
        int16_t current = bq27220_get_current(bq27220);
        uint16_t voltage = bq27220_get_voltage(bq27220);
        uint16_t soc = bq27220_get_state_of_charge(bq27220);
        level = (int)soc;
        // ESP_LOGI(TAG,"=== 电池状态 ===\n");
        // ESP_LOGI(TAG,"电量: %d%%\n", soc);
        // ESP_LOGI(TAG,"电压: %dmV\n", voltage);
        // ESP_LOGI(TAG,"电流: %dmA\n", current);
        // ESP_LOGI(TAG,"温度: %d°C\n", bq27220_get_temperature(bq27220) / 10 - 273);
        
        // 充电状态判断
        if (status.FC) {
            //ESP_LOGI(TAG,"状态: 🔋 已充满\n");
        } else if (current > 20) {
            //ESP_LOGI(TAG,"状态: ⚡ 充电中 (电流: %dmA)\n", current);
            charging = true;
            discharging = false;
        } else if (current < -20) {
            //ESP_LOGI(TAG,"状态: 🔌 放电中 (电流: %dmA)\n", current);
            charging = false;
            discharging = true;
        } else {
            //ESP_LOGI(TAG,"状态: ⏸️  空闲\n");
            charging = false;
            discharging = true;
        }
        
        // 其他状态标志
        // ESP_LOGI(TAG,"标志: %s%s%s%s\n",
        //     status.DSG ? "[放电]" : "",
        //     status.CHGINH ? "[充电禁止]" : "",
        //     status.BATTPRES ? "[电池存在]" : "[无电池]",
        //     status.OTC || status.OTD ? "[过温]" : "");
        return true;
    }

    virtual Backlight* GetBacklight() override {
        if (DISPLAY_LEFT_BACKLIGHT_PIN != GPIO_NUM_NC) {
            static PwmBacklight backlight(DISPLAY_LEFT_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
            //static PwmBacklight backlight_2(DISPLAY_RIGHT_BACKLIGHT_PIN ,DISPLAY_BACKLIGHT_OUTPUT_INVERT);
            return &backlight;
        }
        return nullptr;
    }
};

DECLARE_BOARD(CyberAiDualScreen);