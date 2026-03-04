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

如需我可以：

- 将 `config.h` 的引脚表格化并插入主 README；
- 为 `ws2812b_controller` 和 `touch` 添加快速测试命令或示例代码。