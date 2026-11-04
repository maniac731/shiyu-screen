# ShiYu Screen｜食愈家庭饮食终端

基于 Waveshare ESP32-S3-Touch-LCD-7B 的家庭饮食信息屏原型。当前目标是验证：用户无需 TF 卡，通过手机即可把照片无线发送到屏幕，并在断电后保留。

## 当前已实现

- ESP32-S3 创建本地 Wi-Fi 热点。
- 手机浏览器打开上传页面并从相册选图。
- 手机端自动将图片裁成 1024 × 600。
- 图片转换为 RGB565 后传输并全屏显示。
- 最后一张图片保存到板载 Flash，断电重启后恢复。
- 已在真实 ESP32-S3-Touch-LCD-7B 上完成编译、烧录和无线传图验证。

## 快速开始

Arduino 程序位于：

```text
firmware/wireless_photo_display/
```

详细的 Arduino IDE 配置和使用步骤见：

[firmware/wireless_photo_display/README.md](firmware/wireless_photo_display/README.md)

## 当前产品边界

- 屏幕端以信息展示为主。
- 硬件交互限定为点击、翻页和简单滑动。
- 食材录入等复杂操作优先由手机端完成。
- 当前版本仅保存最后一张图片，不依赖 TF 卡、路由器或云服务。

## 下一轮迭代

- JPEG 压缩存储，目标保留最近 20 张图片。
- 屏幕左右翻页查看历史图片。
- 手机网页查看和删除图片。
- 上传期间显示黑色遮罩和真实进度条，完成后一次性切换画面。
- 上传失败时保留上一张有效图片。

## 项目结构

```text
shiyu-screen/
├── firmware/
│   └── wireless_photo_display/  # 当前可烧录 MVP
├── docs/
│   ├── architecture.md          # 软硬件与数据流说明
│   └── upstream.md              # 第三方代码来源
├── LICENSE
└── README.md
```

## 开源许可

项目采用 Apache License 2.0。部分屏幕底层驱动和绘图代码源自 Waveshare 官方示例，具体来源见 [docs/upstream.md](docs/upstream.md)。
