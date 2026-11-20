# ShiYu Screen｜食愈家庭饮食终端

基于 Waveshare ESP32-S3-Touch-LCD-7B 的家庭饮食信息屏原型。当前主任务是建立 Supabase 后端，打通手机编辑家庭数据、上传照片与屏幕同步。

## 当前开发入口

- `firmware/shiyu_idf/`：当前目标固件；30MHz + DOUBLE_PARTIAL 双 framebuffer 已获用户显示与切页确认。
- `supabase/`：数据库迁移、权限策略与服务端函数骨架。
- `web/`：手机网页端骨架，尚未实现运行页面。
- [后端实施入口](docs/backend/README.md)：数据边界、权限和首阶段验收。

后端当前仅完成仓库目录与配置模板，尚未部署。下方无线传图说明为早期 Arduino MVP 历史记录，不代表当前 IDF 的产品入口。

## 历史 Arduino MVP 已实现

- ESP32-S3 创建本地 Wi-Fi 热点。
- 手机浏览器打开上传页面并从相册选图。
- 手机端自动将图片裁成 1024 × 600。
- 手机端将图片压缩为 JPEG 后传输并全屏显示。
- 板载 Flash 保存最近 20 张照片，并保留约 2.9MB 安全空间。
- 屏幕左右滑动或点击两侧即可翻页。
- 手机网页可查看相册、指定显示和删除照片。
- 上传期间显示黑色遮罩和真实进度，完整校验后才切换图片。
- 当前显示照片会被记录，断电重启后恢复。
- 已在真实 ESP32-S3-Touch-LCD-7B 上完成编译、烧录和无线传图验证。

## 历史 Arduino MVP 快速开始

Arduino 程序位于：

```text
firmware/wireless_photo_display/
```

详细的 Arduino IDE 配置和使用步骤见：

[firmware/wireless_photo_display/README.md](firmware/wireless_photo_display/README.md)

长期 GUI、环境轮播、主动导航和夜间息屏的决策见：

[docs/technical-roadmap.md](docs/technical-roadmap.md)

## 历史 Arduino MVP 产品边界

- 屏幕端以信息展示为主。
- 硬件交互限定为点击、翻页和简单滑动。
- 食材录入等复杂操作优先由手机端完成。
- 历史记录含单图与多图阶段，具体能力以目标路线源码和验收记录为准。

## 历史 Arduino MVP 待真机验收记录

- 不同类型照片的 JPEG 色彩与方向。
- 连续上传多张照片后的排序与自动清理。
- 屏幕左右翻页的方向和手势阈值。
- 上传期间遮罩是否完全消除旧版抖动。

## 项目结构

```text
shiyu-screen/
├── firmware/
│   ├── wireless_photo_display/  # Arduino 无线传图基线
│   ├── shiyu_lvgl/              # Arduino LVGL 原型
│   └── shiyu_idf/               # 当前 ESP-IDF 固件
├── supabase/                   # migrations / functions / tests
├── web/                        # 手机网页端骨架
├── docs/
│   ├── architecture.md          # 软硬件与数据流说明
│   ├── technical-roadmap.md     # 长期 GUI 技术路线与代码设计
│   └── upstream.md              # 第三方代码来源
├── LICENSE
└── README.md
```

## 开源许可

项目采用 Apache License 2.0。部分屏幕底层驱动和绘图代码源自 Waveshare 官方示例，具体来源见 [docs/upstream.md](docs/upstream.md)。
