# 双屏显示板项目 (Binocular Display Board Project)

## 项目概述

双屏显示板项目是一个基于ESP32-S3的双屏幕设备，支持显示表情、文本消息、状态信息等，具备网络连接、语音交互、姿态检测等功能。

## 目录结构

```
/esp-project/binocular-display-of-lily-rabbit/
├── main/
│   ├── boards/dual-screen/    # 双屏板相关代码
│   ├── display/               # 显示相关代码
│   ├── audio/                 # 音频相关代码
│   ├── led/                   # LED相关代码
│   └── ...                    # 其他组件
├── left/                      # 左侧屏幕表情资源
├── scripts/                   # 脚本文件
├── CMakeLists.txt             # 构建配置
└── README.md                  # 项目说明文档
```

## 双屏板硬件介绍

### 主要硬件特性

- **主控芯片**: ESP32-S3 (QFN56)
- **内存**: 8MB PSRAM
- **显示**: 双1.3英寸GC9D01 LCD屏幕 (240x240分辨率)
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

双屏板支持自定义GIF表情，主要通过以下方式配置：

1. **left目录**: 将左侧屏幕的表情GIF文件放在项目根目录的`left`文件夹中
2. **表情命名**: 表情文件需要使用特定的名称，如`happy.gif`、`sad.gif`等
3. **表情尺寸**: 表情文件尺寸应为160x160像素
4. **支持的表情名称**: neutral, happy, laughing, funny, sad, angry, crying, loving, embarrassed, surprised, shocked, thinking, winking, cool, relaxed, delicious, kissy, confident, sleepy, silly, confused
更多配置在README_FIX_EMOJI.md中

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

1. 将自定义GIF表情文件放入项目根目录的`left`文件夹中
2. 确保表情文件名称与支持的表情名称一致
3. 重新构建和烧录项目

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
