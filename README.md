# 双屏显示板项目 (Binocular Display Board Project)

## 项目概述

双屏显示板项目是一个基于ESP32-S3的双屏幕设备，支持显示表情、文本消息、状态信息等，具备网络连接、语音交互、姿态检测等功能。

## 目录 (Table of Contents)

1. [项目概述](#项目概述)
2. [目录结构](#目录结构)
3. [双屏板硬件介绍](#双屏板硬件介绍)
4. [软件功能](#软件功能)
   - [核心功能](#核心功能)
   - [表情资源配置](#表情资源配置)
   - [背光调整](#背光调整)
5. [快速开始指南](#快速开始指南)
6. [开发指南](#开发指南)
7. [常见问题](#常见问题)
8. [版本历史](#版本历史)
9. [贡献指南](#贡献指南)
10. [许可证](#许可证)
11. [联系方式](#联系方式)
12. [致谢](#致谢)


## 目录结构

```

- 板级详细说明: [docs/board-cyberai-toy-lily-4G-2.md](docs/board-cyberai-toy-lily-4G-2.md)
/esp-project/binocular-display-of-lily-rabbit/
├── main/
│   ├── boards/cyberai-toy-lily-4G-2/    # 双屏板相关代码
│   ├── display/               # 显示相关代码
│   ├── audio/                 # 音频相关代码
│   ├── led/                   # LED相关代码
│   └── ...                    # 其他组件
├── left/                      # 左侧屏幕表情资源
├── scripts/                   # 脚本文件
├── CMakeLists.txt             # 构建配置
└── README.md                  # 项目说明文档

# 板级说明 — main/boards/cyberai-toy-lily-4G-2

下面是 `main/boards/cyberai-toy-lily-4G-2` 目录中主要文件的结构与功能说明，便于开发者定位与定制。

- document.md: 板级用户与功能说明（按钮、触摸、RGB 灯光、模式切换等）。
- config.h: 板级引脚与常量配置（显示引脚、I2S 音频引脚、I2C、ADC 通道、电池电压定义、4G UART、触摸通道、RGB 引脚等）。修改硬件映射优先在此文件进行。
- dual-screen.cc: 该板的主实现文件，包含 `CyberAiDualScreen` 类。负责：
  - 初始化 I2C/SPI/ADC、显示驱动（双屏 GC9D01）、按键与触摸回调。  
  - IMU（QMI8658）初始化与摇晃/撞击/方向检测逻辑。  
  - BMS（bq27220）电量管理与 ADC 校准/读取。  
  - 网络类型切换（WiFi / ML307 4G）和按键双击逻辑。  

- qmi8658.c / qmi8658.h: QMI8658 IMU 驱动，提供初始化、加速度/陀螺/温度读取、传感器配置和唤醒功能。用于姿态与摇晃检测。
- touch.cc / touch.h: 铜箔触摸传感器逻辑，包含通道初始化、中断/队列处理、去抖与触摸计数，以及触摸触发的情绪反应（显示/唤醒/语音）。
- ws2812b_controller.cc / ws2812b_controller.h: WS2812B 灯带控制器的面向对象实现。提供：多种动画（呼吸、彩虹、追逐、闪烁）、亮度控制、根据设备状态自动映射颜色，以及通过 MCP 暴露的远程控制工具。
- led.c / led.h: 基于 RMT 的简单 LED demo 任务，适合硬件验证或调试。

如何定制：

- 更改引脚或外设映射：编辑 `config.h`，并在 `dual-screen.cc` 中确认初始化使用相同常量。  
- 修改触摸反应：在 `touch.cc` 中调整反应文本与触发阈值。  
- 添加 LED 动画：在 `ws2812b_controller.*` 中实现新动画并在 Board 初始化中使用或通过 MCP 注册。  
- 调整姿态检测：使用 `qmi8658` 提供的接口并在 `dual-screen.cc` 中修改阈值与滤波参数。

快速检查点：

- 首先查看 `docs/board-cyberai-toy-lily-4G-2.md`（本文件）和 `main/boards/cyberai-toy-lily-4G-2/config.h`。 
- 需要修改运行时行为时优先在 `dual-screen.cc` 中查找相应初始化或回调代码。

```

## 双屏板硬件介绍

### 主要硬件特性

- **主控芯片**: ESP32-S3 (QFN56)
- **内存**: 8MB PSRAM
- **显示**: 双0.71英寸GC9D01 LCD屏幕 (160x160分辨率)
- **触摸**: 支持触摸输入
- **音频**: 内置音频编解码器和麦克风
- **传感器**: QMI8658 6轴姿态传感器
- **网络**: 支持WiFi和4G网络 (ML307)
- **电源**: 支持电池供电和充电管理

### 硬件连接

| 功能 | 引脚 | 说明 |
|------|------|------|
| 显示左CS | DISPLAY_LEFT_SPI_CS_PIN | 左侧屏幕SPI片选引脚 |
| 显示右CS | DISPLAY_RIGHT_SPI_CS_PIN | 右侧屏幕SPI片选引脚 |
| 显示DC | DISPLAY_SPI_DC_PIN | 显示数据/命令引脚 |
| 显示复位 | DISPLAY_SPI_RESET_PIN | 显示复位引脚 |
| 显示左背光 | DISPLAY_LEFT_BACKLIGHT_PIN | 左侧屏幕背光引脚 |
| 显示右背光 | DISPLAY_RIGHT_BACKLIGHT_PIN | 右侧屏幕背光引脚 |
| 音频I2C SDA | AUDIO_CODEC_I2C_SDA_PIN | 音频编解码器I2C数据引脚 |
| 音频I2C SCL | AUDIO_CODEC_I2C_SCL_PIN | 音频编解码器I2C时钟引脚 |
| 姿态传感器I2C | 与音频编解码器共用 | QMI8658使用与音频编解码器相同的I2C总线 |
| 4G模块TX | ML307_TX_PIN | 4G模块UART发送引脚 |
| 4G模块RX | ML307_RX_PIN | 4G模块UART接收引脚 |
| 4G使能 | EN_4G | 4G模块使能引脚 |
| 启动按钮 | BOOT_BUTTON_GPIO | 设备启动/配置按钮 |

## 软件功能

### 核心功能

1. **双屏显示**: 支持在两个屏幕上显示不同内容
2. **表情显示**: 支持显示自定义GIF表情
3. **文本消息**: 支持显示文本消息
4. **状态显示**: 支持显示设备状态信息
5. **网络连接**: 支持WiFi和4G网络连接
6. **语音交互**: 支持语音唤醒和语音命令
7. **姿态检测**: 支持摇晃、撞击和方向改变检测
8. **触摸交互**: 支持触摸输入
9. **电源管理**: 支持电池电量检测和充电管理


### 表情资源配置

设备支持把自定义的 GIF 表情编译进固件并在运行时显示，
你可以把自己的动画放到根目录的 `left` 文件夹来覆盖默认资源。

**资源要求**

- **格式**: GIF
- **尺寸**: 160×160 像素
- **名称**: 使用表情关键词作为文件名（不带扩展名自动添加 `.gif`）

  支持的预设表情名称列表：
  > neutral, happy, laughing, funny, sad, angry, crying, loving, embarrassed, 
  surprised, shocked, thinking, winking, cool, relaxed, delicious, kissy,
  confident, sleepy, silly, confused

- 单个文件大小建议不超过 500 KB，以免固件体积过大。

**目录结构示例**

```
/esp-project/binocular-display-of-lily-rabbit/
├── left/
│   ├── happy.gif
│   ├── sad.gif
│   └── ... 其他表情文件
├── main/
├── scripts/
└── ... 其他项目文件
```

> 也可以阅读 [`README_FIX_EMOJI.md`](README_FIX_EMOJI.md) 获取更详细的配置指南。

**配置流程**

1. 准备好符合要求的 GIF 文件并放入 `left` 目录。
2. 构建系统会自动扫描该目录，并在 `assets.bin` 中打包这些表情。
   无需手动修改 CMakeLists.txt 或代码。
3. 使用 `idf.py build` 重新生成固件并 `idf.py flash` 烧录。

**验证与故障排除**

- 烧录完成后，设备启动时应能在日志中看到表情加载成功的消息。
- 若表情未显示：
  - 检查 `left` 目录是否存在正确的 GIF 文件。
  - 文件名是否与支持列表一致。
  - 大小是否超过限制或被损坏。
  - 查看构建输出，确认资源已被处理。
  - 使用 `idf.py monitor` 查看运行时日志以定位加载错误。

- 构建失败时，请检查 CMake 文件语法、依赖是否齐全，或参考构建日志。


### 背光调整

双屏板支持调整屏幕背光亮度：

1. **默认亮度**: 默认背光亮度为90（范围0-100）
2. **亮度持久化**: 亮度设置会保存在设备中，重启后恢复
3. **两侧屏幕独立控制**: 左侧和右侧屏幕的背光可以独立调整

### 背光调整

双屏板支持调整屏幕背光亮度：

1. **默认亮度**: 默认背光亮度为90（范围0-100）
2. **亮度持久化**: 亮度设置会保存在设备中，重启后恢复
3. **两侧屏幕独立控制**: 左侧和右侧屏幕的背光可以独立调整

## 快速开始指南

### 1. 准备开发环境

- 安装ESP-IDF 5.5.1
- 配置ESP-IDF环境变量
- 安装必要的依赖

### 2. 构建项目

```bash
# 克隆项目
git clone <项目地址>
cd /esp-project/binocular-display-of-lily-rabbit

#选择板级文件
(Top) → Xiaozhi Assistant → Default Language 
在选择语言后（汉语、英语、俄语）
(Top) → Xiaozhi Assistant → Board Type 
选择WIFI板/4G板


# 构建项目
idf.py build
```

### 3. 烧录固件

```bash
# 烧录固件到设备
idf.py -p /dev/ttyACM0 flash

# 查看设备日志
idf.py -p /dev/ttyACM0 monitor
```

### 4. 配置表情资源

在项目根目录下创建或更新 `left` 文件夹并放入符合要求的 GIF 文件。构建过程会自动包含这些表情。

1. 将自定义 GIF 表情放入 `left` 目录。
2. 确保名称与支持列表一致，并且尺寸为 160×160。
3. 运行 `idf.py build` 并 `idf.py flash` 将固件烧录到设备。
4. 使用 `idf.py monitor` 检查启动日志，确认表情加载成功。

如果需要更详细的配置步骤和故障排除建议，请参阅 [`README_FIX_EMOJI.md`](README_FIX_EMOJI.md)。

## 开发指南

### 代码结构

- **`main/boards/cyberai-toy-lily-wifi-2/dual-screen.cc`**: 双屏板主程序
- **`main/display/lcd_display.cc`**: LCD显示相关代码
- **`main/display/emote_display.h`**: 表情显示相关代码
- **`main/assets.cc`**: 资源加载相关代码
- **`scripts/build_default_assets.py`**: 资源构建脚本

### 主要类和接口

- **`CyberAiDualScreen`**: 双屏板主类，继承自`DualNetworkBoard`
- **`Display`**: 显示基类，定义了显示相关的接口
- **`LcdDisplay`**: LCD显示实现类
- **`EmoteDisplay`**: 表情显示实现类
- **`Backlight`**: 背光控制基类
- **`PwmBacklight`**: PWM背光控制实现类

### 调试技巧

1. **查看设备日志**: 使用`idf.py monitor`查看设备日志
2. **调试模式**: 在CMakeLists.txt中启用调试选项
3. **硬件调试**: 使用ESP32-S3的JTAG接口进行硬件调试
4. **姿态传感器调试**: 可以通过摇晃设备触发姿态检测事件

## 常见问题

### 问题1: 表情不显示

**解决方法**:
- 检查`left`目录中的表情文件是否存在
- 检查表情文件名称是否与支持的表情名称一致
- 检查表情文件尺寸是否为160x160像素
- 查看设备日志，确认表情加载是否成功

### 问题2: 屏幕亮度不够

**解决方法**:
- 检查背光引脚配置是否正确
- 修改`backlight.cc`中的默认亮度值
- 确保两侧屏幕的背光都被正确初始化

### 问题3: 网络连接失败

**解决方法**:
- 检查WiFi或4G模块配置
- 确认网络信号强度
- 查看设备日志，确认网络连接过程

### 问题4: 构建失败

**解决方法**:
- 检查ESP-IDF版本是否正确
- 确认所有依赖项都已正确安装
- 查看详细的构建日志，定位错误原因

## 版本历史

| 版本 | 日期 | 主要变更 |
|------|------|----------|
| 1.0.0 | 2025-12-08 | 初始版本 |
|      |      | 支持双屏显示 |
|      |      | 支持自定义表情 |
|      |      | 支持姿态检测 |
|      |      | 支持WiFi和4G网络 |

## 贡献指南

欢迎提交Issue和Pull Request来帮助改进项目。

### 提交Pull Request

1. Fork项目
2. 创建特性分支
3. 提交代码变更
4. 确保代码通过构建测试
5. 提交Pull Request

### 代码规范

- 遵循ESP-IDF代码规范
- 使用C++17特性
- 添加必要的注释
- 保持代码简洁和可读性

## 许可证

本项目采用Apache License 2.0许可证。

## 联系方式

如有问题或建议，请通过以下方式联系：

- 项目地址: <项目地址>
- 邮箱: <联系邮箱>

## 致谢
