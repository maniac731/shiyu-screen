# 食愈 ESP-IDF 迁移基线

本工程与 `firmware/shiyu_lvgl` 并存。Arduino 工程保留为可运行回退版本，迁移期间不覆盖、不删除。

## 当前阶段

阶段 1：静态显示稳定性基线。

- ESP-IDF v5.5.6
- ESP32-S3
- LVGL 9
- 1024×600 RGB565
- 30 MHz RGB pixel clock
- 双 framebuffer（PSRAM）
- 20 行 bounce buffer
- 64B data cache line
- Flash / PSRAM XIP
- Flash / PSRAM 120 MHz
- 16MB Flash
- 3MB 应用分区 + FAT 照片存储分区

当前固件只显示一张深色静态诊断页。此阶段不迁移 Wi-Fi、照片上传、相册轮播或正式产品页面，避免同时改变过多变量。

## 阶段 1 验收

1. 工程能以 ESP-IDF v5.5.6 独立编译。
2. 烧录后静态深色页面连续显示 15 分钟。
3. 近距离观察无持续闪烁、位置抖动、错行、漂移或撕裂。
4. 触摸初始化不导致显示异常。

阶段 1 已通过，进入阶段 2：恢复 HOME / SETTINGS / AMBIENT 最小状态原型。

## 阶段 2 验收

1. HOME、SETTINGS、AMBIENT 是相互独立的 LVGL 页面。
2. 设置、返回和 Ambient 全屏唤醒使用触摸事件。
3. HOME 无操作 60 秒后自动进入 Ambient。
4. 本阶段保持 Wi-Fi 和照片上传关闭，单独验收 UI 性能。
5. 连续切页仍能响应，且不闪屏、错行、漂移、撕裂或残留旧画面。

## 构建

```bash
eim select v5.5.6
cd /Users/mengxiaopeng/Desktop/shiyu-screen/firmware/shiyu_idf
idf.py set-target esp32s3
idf.py build
```

## 烧录

```bash
idf.py -p /dev/cu.usbmodem5C931633361 flash monitor
```

退出串口监视器：`Control + ]`。
