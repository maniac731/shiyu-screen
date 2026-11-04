# 第三方代码来源

屏幕初始化、I2C、IO 扩展、绘图和字体相关文件基于 Waveshare 官方工程示例：

- 项目：`waveshareteam/ESP32-S3-Touch-LCD-7B`
- 产品：ESP32-S3-Touch-LCD-7B
- 上游示例：`examples/Arduino/examples/11_WIFI_STA`
- 许可：Apache License 2.0

当前保留的上游衍生文件包括：

```text
Debug.h
font24.cpp
fonts.h
gui_paint.cpp
gui_paint.h
i2c.cpp
i2c.h
io_extension.cpp
io_extension.h
rgb_lcd_port.cpp
rgb_lcd_port.h
```

本项目新增的无线上传网页、手机端裁剪与 RGB565 转换、HTTP 接收、FATFS 持久化和产品流程位于 `wireless_photo_display.ino`。
