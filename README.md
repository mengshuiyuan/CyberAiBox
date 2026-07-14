# 赛博盒子（Cyber AI Box 1.28）

基于 **ESP32-S3** 的双目 AI 语音交互设备，支持双圆屏表情同显、Wi‑Fi / 4G 联网、语音唤醒、铜箔触摸、姿态检测与 RGB 灯带。

当前固件版本：`2.0.3.4`

---

## 目录

1. [项目概述](#项目概述)
2. [硬件规格](#硬件规格)
3. [硬件资料](#硬件资料)
4. [引脚映射](#引脚映射)
5. [交互与功能](#交互与功能)
6. [目录结构](#目录结构)
7. [板级说明](#板级说明)
8. [快速开始](#快速开始)
9. [表情资源配置](#表情资源配置)
10. [开发指南](#开发指南)
11. [常见问题](#常见问题)
12. [版本历史](#版本历史)
13. [许可证](#许可证)

---

## 项目概述

赛博盒子是面向桌面/玩具形态的双屏 AI 设备，在小智助手框架上扩展了双屏显示与多种传感器交互：

| 能力 | 说明 |
|------|------|
| 双屏表情 | 双 GC9D01 圆屏同显，支持自定义 GIF 表情 |
| 语音交互 | 「你好小智」唤醒，ES8311 + ES7210 编解码 |
| 联网 | Wi‑Fi 与 ML307 4G 双模（可切换） |
| 传感交互 | 铜箔摸头、摇晃/撞击检测（QMI8658） |
| 灯光 | 4 颗 WS2812B，支持状态联动与语音控灯 |
| 电源 | BQ27220 电量计，约 1200 mAh，低电自动关机 |

板型目录前缀：`cyber-ai-box-1.28-*`（Wi‑Fi / 4G × 中 / 英 / 俄）。

---

## 硬件规格

| 项目 | 规格 |
|------|------|
| 主控 | ESP32-S3（目标芯片 `esp32s3`） |
| Flash | 16 MB（QIO） |
| PSRAM | 八线 Octal，80 MHz |
| 显示 | 双圆屏 GC9D01，分辨率 **240×240**，SPI |
| 音频 | ES8311（播放）+ ES7210（麦阵），采样率 24 kHz |
| 姿态 | QMI8658 六轴 IMU（与 Codec 共用 I2C） |
| 电量 | BQ27220，设计容量 1200 mAh；满电约 4.1 V，放空约 3.2 V |
| 网络 | Wi‑Fi；4G 模组 ML307（UART） |
| 触摸 | 铜箔触摸（Touch Pad 通道 4 / 5） |
| RGB | WS2812B × 4，GPIO15 |
| 按键 | 开机键（长按约 3 s）；唤醒键（BOOT，GPIO0） |

实物示意（板级文档附图）：

![赛博盒子实物图](main/boards/cyber-ai-box-1.28-4g/dual-screen-box.png)

---

## 硬件资料

硬件设计文件位于仓库根目录 `赛博盒子硬件/`：

| 文件 | 说明 |
|------|------|
| `SCH_原理图_2025-12-05.pdf` | 原理图（2025-12-05） |
| `BOM_工业板_工业板V1.4_2025-12-05.xlsx` | 工业板 BOM V1.4 |
| `BOM_双目盒子模组.xlsx` | 双目盒子模组 BOM |

引脚与常量以板级 `config.h` 为准；原理图与 BOM 用于贴片、采购与硬件排查。

---

## 引脚映射

以下摘自 `main/boards/cyber-ai-box-1.28-4g/config.h`（Wi‑Fi 板与 4G 板引脚定义一致；4G 相关脚仅在 4G 板启用）。

### 显示（SPI）

| 功能 | GPIO | 说明 |
|------|------|------|
| SPI SCLK | 18 | 时钟 |
| SPI MOSI | 17 | 数据 |
| DC | 3 | 数据/命令 |
| RESET | 16 | 复位 |
| 左屏 CS | 8 | 左屏片选 |
| 右屏 CS | 9 | 右屏片选 |
| 左屏背光 | 46 | PWM 背光 |
| 右屏背光 | 10 | PWM 背光 |

### 音频（I2S + I2C）

| 功能 | GPIO | 说明 |
|------|------|------|
| MCLK | 1 | |
| WS | 44 | |
| BCLK | 43 | |
| DIN | 2 | 录音 |
| DOUT | 42 | 播放 |
| Codec I2C SDA | 7 | ES8311 / ES7210 / QMI8658 / BQ27220 共用 |
| Codec I2C SCL | 6 | |
| PA 使能 | 48 | 功放 |

### 网络 / 电源 / 其它

| 功能 | GPIO | 说明 |
|------|------|------|
| BOOT / 唤醒键 | 0 | 单击切换对话；双击切换网络/配网 |
| 4G 使能 | 14 | 低电平导通相关逻辑，初始化时拉高使能 |
| ML307 TX | 38 | 4G UART |
| ML307 RX | 39 | 4G UART |
| 充电完成 | 13 | `CHARGE_STDBY`，低电平表示充满 |
| VBAT ADC | ADC1 CH2 | 电池电压采样 |
| RGB 灯带 | 15 | WS2812B 数据 |
| 触摸通道 | Touch 4 / 5 | 铜箔摸头 |

修改引脚请编辑对应板目录下的 `config.h`，并确认 `dual-screen.cc` 初始化与之一致。

---

## 交互与功能

### 按键

- **开机键（右）**：长按约 3 秒开机。
- **唤醒键（左，BOOT）**：
  - 单击：切换对话状态 / 唤醒。
  - 开机过程中双击：在 **4G ↔ Wi‑Fi** 之间切换。
  - Wi‑Fi 模式下双击：重新进入配网。

### 唤醒方式

| 方式 | 操作 |
|------|------|
| 语音 | 说「你好小智」 |
| 按键 | 单击唤醒键 |
| 触摸 | 摸头顶铜箔 |
| 摇晃 | 摇晃机身（IMU） |

### RGB 灯带（语音控制）

默认开启**按设备状态自动变色**：

| 状态 | 灯光 |
|------|------|
| 关机 | 熄灭 |
| 开机中 | 黄 |
| 待机 | 蓝 |
| 说话 | 绿 |
| 聆听 | 红 |

常用口令（需先「关闭灯光自动控制」后再用手动效果）：

- 「开启/关闭灯光自动控制」
- 「开灯 / 关灯」
- 「进行呼吸灯效果」「彩虹效果」「追逐效果」「闪烁效果」
- 「设置灯光亮度为 30」（亮度 1–100）

### 铜箔触摸

默认开启；可用「开启/取消触摸」开关。触摸会触发表情与语音回应（开心 / 生气 / 思考等）。

### 电量

- 询问：「你现在有多少电量？」
- **电量低于 10% 且未充电时，设备会自动关机。**

### 背光

- 默认亮度约 90（范围 0–100），可持久化。
- 左右屏背光可独立控制。

---

## 目录结构

```
dual-screen-box-box-1.28/
├── main/
│   ├── boards/
│   │   ├── cyber-ai-box-1.28-wifi/      # Wi‑Fi · 中文
│   │   ├── cyber-ai-box-1.28-wifi-en/   # Wi‑Fi · 英语
│   │   ├── cyber-ai-box-1.28-wifi-ru/   # Wi‑Fi · 俄语
│   │   ├── cyber-ai-box-1.28-4g/        # 4G · 中文（默认）
│   │   ├── cyber-ai-box-1.28-4g-en/     # 4G · 英语
│   │   ├── cyber-ai-box-1.28-4g-ru/     # 4G · 俄语
│   │   ├── common/                     # 板级公共代码
│   │   └── echoear/                    # 其它板型
│   ├── display/                        # LCD / 表情显示
│   ├── audio/                          # 音频处理
│   └── ...
├── left/                               # 自定义 GIF 表情（构建时打包）
├── scripts/                            # 资源构建、发布等脚本
├── partitions/                         # 分区表
├── docs/                               # 协议与补充文档
├── 赛博盒子硬件/                        # 原理图、BOM
├── CMakeLists.txt
├── sdkconfig.defaults.esp32s3
└── README.md
```

---

## 板级说明

以 `main/boards/cyber-ai-box-1.28-4g/` 为例（其它语言/网络变体结构相同）：

| 文件 | 作用 |
|------|------|
| `document.md` | 用户侧功能说明（按键、灯光、触摸、唤醒等） |
| `config.h` | 引脚与常量；改硬件映射优先改这里 |
| `dual-screen.cc` | 板级主实现 `CyberAiDualScreen`：双屏、IMU、BMS、按键、4G/Wi‑Fi、灯带 |
| `bq27220/` | 电量计驱动 |
| `dual-screen-box.png` | 实物图 |

公共组件（触摸、WS2812、IMU 等）多位于 `main/boards/common/` 或板目录旁的共享源文件中。

**定制建议：**

- 改引脚 → `config.h` + 核对 `dual-screen.cc`
- 改触摸话术/阈值 → `touch` 相关实现
- 加灯效 → `ws2812b_controller`
- 调姿态阈值 → `dual-screen.cc` 中摇晃/撞击参数

---

## 快速开始

### 1. 环境

- 安装 [ESP-IDF 5.5.x](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/get-started/index.html)（建议与团队统一的 5.5.1）
- 目标芯片：`esp32s3`
- 配置好 `IDF_PATH` 与导出脚本（Windows 可用 ESP-IDF PowerShell / CMD）

### 2. 选择语言与板型

```bash
cd dual-screen-box-box-1.28
idf.py set-target esp32s3
idf.py menuconfig
```

路径：

1. `Xiaozhi Assistant` → `Default Language`：中文 / 英语 / 俄语  
2. `Xiaozhi Assistant` → `Board Type`：
   - `双目盒子WIFI_中文` / `_英语` / `_俄语`
   - `双目盒子4G-中文` / `-英语` / `-俄语`（默认多为 4G 中文）

语言与板型需匹配（例如英语板依赖 `LANGUAGE_EN_US`）。

### 3. 构建与烧录

```bash
# 构建
idf.py build

# 烧录（按实际串口修改）
# Linux / macOS 示例：
idf.py -p /dev/ttyACM0 flash monitor

# Windows 示例：
idf.py -p COM3 flash monitor
```

退出串口监视：`Ctrl+]`。

### 4. 配网

Wi‑Fi 模式下双击唤醒键进入配网（SoftAP 或 Blufi，取决于 menuconfig 中的 Net Configuration Mode）。

---

## 表情资源配置

双屏采用**同显**：将 GIF 放入项目根目录 `left/`，构建时自动打入资源。

**要求：**

- 格式：GIF  
- 建议尺寸：**160×160**（显示在 240×240 圆屏上）  
- 文件名使用预设表情名（构建时补 `.gif`）  
- 单文件建议不超过 500 KB  

**支持的表情名：**

`neutral` `happy` `laughing` `funny` `sad` `angry` `crying` `loving` `embarrassed` `surprised` `shocked` `thinking` `winking` `cool` `relaxed` `delicious` `kissy` `confident` `sleepy` `silly` `confused`

```
left/
├── happy.gif
├── sad.gif
└── ...
```

更细的步骤与排错见 [`README_FIX_EMOJI.md`](README_FIX_EMOJI.md)。

流程：放入 GIF → `idf.py build` → `idf.py flash` → `idf.py monitor` 确认加载日志。

---

## 开发指南

### 关键类

| 类 | 说明 |
|----|------|
| `CyberAiDualScreen` | 板级主类，继承 `DualNetworkBoard` |
| `SpiLcdDisplay` / `LcdDisplay` | LCD 显示 |
| `EmoteDisplay` | 表情显示 |
| `PwmBacklight` | PWM 背光 |
| `WS2812BController` | RGB 灯带 |
| `BoxAudioCodec` | ES8311 + ES7210 |

### 调试

1. `idf.py monitor` 查看启动、网络、表情、IMU 日志  
2. 摇晃设备验证姿态事件  
3. 需要时可启用 CMake / sdkconfig 中的调试选项  
4. 硬件级调试可用 ESP32-S3 JTAG  

### 相关脚本

- `scripts/build_default_assets.py`：默认资源打包  
- `scripts/release.py`：按板型发布打包（自定义板务必使用独立板名，避免 OTA 通道冲突）

---

## 常见问题

### 表情不显示

- 确认 `left/` 下文件存在，且文件名在支持列表中  
- 尺寸与格式是否符合要求  
- 查看 monitor 中资源加载日志  

### 屏幕偏暗 / 不亮

- 检查左右背光脚 GPIO46 / GPIO10  
- 确认背光已初始化且亮度未设为 0  

### 网络连不上

- Wi‑Fi：信号与配网流程；4G：SIM、天线、`EN_4G`（GPIO14）与 UART（38/39）  
- 确认 menuconfig 所选板型与实物一致（Wi‑Fi 板 / 4G 板）  

### 构建失败

- 确认 IDF 版本与 `set-target esp32s3`  
- 语言与 Board Type 组合是否合法  
- 阅读完整构建日志定位缺失组件  

### 低电自动关机

- 设计行为：SOC 低于 10% 且未充电会关机；请接入充电后重试  

---

## 版本历史

| 版本 | 日期 | 说明 |
|------|------|------|
| 2.0.3.4 | — | 当前工程版本（见根目录 `CMakeLists.txt` 中 `PROJECT_VER`） |
| 1.0.0 | 2025-12-08 | 初版文档：双屏、自定义表情、姿态、Wi‑Fi/4G |

---

## 许可证

本项目采用 [MIT License](LICENSE)。

版权声明见 `LICENSE` 文件（含 Shenzhen Xinzhi Future Technology Co., Ltd. 及贡献者）。

---

## 致谢

感谢乐鑫 ESP-IDF、LVGL、小智开源生态及相关驱动与模组厂商的支持。
