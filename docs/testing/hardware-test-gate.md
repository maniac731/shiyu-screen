# 食愈硬件屏幕测试门禁

## 实验与环境

- 日期、唯一结果：
- 目标固件：`wireless_photo_display` / `shiyu_lvgl` / `shiyu_idf`
- 起始提交与工作树状态：
- 修改区域、回退点：
- 是否可能清空 FATFS/照片：否 / 是（需用户确认）
- 板型：Waveshare ESP32-S3-Touch-LCD-7B
- 串口、框架/LVGL 版本：
- Flash Size、分区、PSRAM/Flash 频率：
- Pixel clock、framebuffer、bounce buffer：

## 构建与烧录

- 实际编译命令、结果、日志：
- Flash / SRAM / PSRAM 用量：
- 已核对目标板、串口、固件路线、分区和 PSRAM：是 / 否
- 可能清空照片时已获用户确认：不适用 / 是 / 否
- 烧录状态：未烧录 / 失败 / 已烧录并校验
- 串口启动证据：

ESP-IDF 当前基线：

```bash
eim select v5.5.6
cd firmware/shiyu_idf
idf.py set-target esp32s3
idf.py build
```

## 真机矩阵

| 方向 | 场景 | 标准 | 次数/时长 | 实际 | 状态 |
|---|---|---|---|---|---|
| 显示 | 深色静态页 | 无持续闪烁、抖动、错行、漂移或撕裂 | 15 分钟 | | 未运行 |
| 显示 | 浅色页与照片 | 颜色、方向、裁切正确，无残影 | 多素材 | | 未运行 |
| 触摸 | 点击、返回、连续切页 | 100ms 内反馈，普通切页 ≤500ms | 20 次 | | 未运行 |
| 状态 | HOME/SETTINGS/AMBIENT | 路径和返回可重复 | 10 轮 | | 未运行 |
| 网络 | 进入/退出手机管理 | 热点、背光和 UI 正确恢复 | 5 轮 | | 未运行 |
| 上传 | 成功与失败 | 进度真实；失败保留上一有效图 | 各 3 次 | | 未运行 |
| 存储 | 重启、20 张/7MB 边界 | 恢复与清理规则正确 | 达到边界 | | 未运行 |
| 稳定性 | Wi-Fi/Flash/JPEG 并发 | UI 不长阻塞，显示不明显恶化 | 连续操作 | | 未运行 |

## 结论

- `implemented` / `compiled` / `flashed` / `device-tested`：
- `accepted`：只能由用户确认
- 未验证项、失败复现、回退方法、用户下一步观察：
