# 第三方代码来源

屏幕初始化、I2C、IO 扩展、绘图和字体相关文件基于 Waveshare 官方工程示例：

- 项目：`waveshareteam/ESP32-S3-Touch-LCD-7B`
- 产品：ESP32-S3-Touch-LCD-7B
- 上游示例：`examples/Arduino/examples/11_WIFI_STA`
- 许可：Apache License 2.0

LVGL 最小原型 `firmware/shiyu_lvgl` 基于同一官方工程中的
`examples/Arduino/examples/16_LVGL_V9_DEMO`，保留其 LCD、GT911、I2C、IO 扩展和
LVGL port，并沿用本项目真机验证的 18MHz 像素时钟与 20 行 bounce buffer。

GUI 框架使用厂商包附带的 LVGL 9.5.0，LVGL 本身采用 MIT License。

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
gt911.cpp
gt911.h
touch.cpp
touch.h
```

本项目新增的无线上传网页、手机端裁剪与 JPEG 压缩、HTTP 接收、FATFS 相册持久化和产品流程位于 `wireless_photo_display.ino`。

JPEG 解码使用 Arduino Library Manager 中的 `TJpg_Decoder` 1.1.0（Bodmer），项目地址：`https://github.com/Bodmer/TJpg_Decoder`。
