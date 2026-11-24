# 云照片下载与旧热点缓存移除

- 目标：`shiyu_idf` 已绑定屏幕显示厨房最新云照片，不再回退热点上传图片。
- 真机原故障：manifest 返回照片，下载 TLS 证书校验成功后出现 `HTTP_CLIENT: Out of buffer`；UI 为 `Photo rejected`。IDF 5.5.6 的 `esp_http_client.c` 请求行生成使用 TX 缓冲拼接路径和签名 query，旧固定512字节不足。
- 修复：TX 按 URL 长度+32字节分配，最少512、上限4096；不打印签名URL。下载失败和JPEG校验/缓存提交失败分开记录。
- 清理：移除本路线未编译的 photo_portal 源码和旧上传接受接口。启动只删除旧 current.jpg、upload.tmp；新临时文件 cloud.tmp。云 A/B 槽保留用于断网展示和安全替换，不自动格式化、不改变分区。
- 显示基线：30MHz、DOUBLE_PARTIAL、双framebuffer、20行bounce、120MHz PSRAM不改。
- 构建：ESP-IDF5.5.6成功，应用 `0x226900`，3MB APP余28%；`git diff --check`通过。
- 回退：恢复本次TX配置及legacy回退代码；已删除的旧热点照片无法靠回退固件恢复，用户已明确要求删除。
- 独立低推理只读复核：GO；动态TX范围与旧缓存清理未发现阻断。
- 烧录：2255104字节应用写入并Hash verified，原分区表校验通过；无整片擦除。
- 真机：启动等待SNTP后API/Storage证书校验通过；约92秒记录 `after-photo-commit` 和 `State: Photo synced`，约106秒 `Photos up to date`。完整JPEG校验、云A/B提交和UI换源链路通过；下载期间出现一次 `HTTP_HEADER: Buffer length is small to fit all the headers`，IDF将请求头分段发送后下载成功，与原请求行 `Out of buffer` 失败不同。短时未见panic/重启；内部剩余约23KB、最大块12800B、PSRAM约3.69MB。
- 状态：implemented / compiled / flashed；device-tested部分通过（下载/校验/提交/后续unchanged）；accepted待人工观察云图正确及无抖动。重启恢复、断网、多次新图替换未验证。
- 人工门禁：新云图正确显示、无黑条/抖动；再次上传后自动替换；断网仍显示最近云图，断电重新启动恢复云缓存。
