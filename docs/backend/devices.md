# 首次屏幕绑定与单张同步契约

状态：后端 migration、两个 Edge Functions、手机绑定界面和固件接入已实现；本文件不代表云端部署或真机通过。当前硬件仍维持已确认的30MHz DOUBLE_PARTIAL双帧基线。

## 设备与接口

设备生成随机UUID和32字节随机secret（编码为64位hex），以单个NVS blob持久化后注册。云端只存SHA-256。二维码含10分钟一次性claim token，不含长期secret。手机扫码登录后仅厨房创建者可预览和明确确认绑定；加入厨房的成员不用再扫码。token泄露可能被其他厨房创建者抢先绑定，首版应只在屏幕主动打开绑定页时显示，不能宣称防截图转发。

`POST /functions/v1/device-api` 使用 `X-Device-Id`、`X-Device-Secret` 请求头，JSON action为enroll/pairing/status/manifest。enroll同id同secret可重试，异secret和已撤销拒绝。pairing更新会废除旧码；manifest可传known_photo_id，返回unchanged/empty/unbound或photo（id、120秒download_url、expires_at、max_bytes=1048576、width=1024、height=600）。尺寸为设备要求，不表示服务器已验证二进制。服务端从绑定记录推导厨房，调用方不能指定任意对象路径或厨房。

`POST /functions/v1/device-owner` 使用用户JWT，经Auth.getUser真实验证；action为preview/claim/list/revoke。claim_token只用于preview/claim，device_id只用于revoke。只返回脱敏设备列表。`public.device_control`仅授权service_role执行，私有表不向客户端开放。所有QR claim/刷新和撤销按设备行串行。

## 部署

1. 执行migration `20261120000400_devices.sql`，一次性测试库运行 `supabase/tests/devices.sql`；两连接竞争claim及到期测试仍需补跑。
2. CLI已登录后分别执行 `supabase functions deploy device-api --project-ref lkewydexsjjntydpbzri --use-api` 与device-owner。config.toml将verify_jwt关闭，两个函数自行验证独立设备secret或真实用户JWT，不能删除这些鉴权代码。
3. 设置云端 `WEB_ORIGIN=https://kitchen.maniac3221225477.sbs`；SUPABASE_URL/SUPABASE_SERVICE_ROLE_KEY使用Edge运行环境内置服务端secret。禁止把service key写入web、固件或Git。生产缺WEB_ORIGIN时当前默认同域名；换域需同时更新。
4. 部署网页最新build。二维码使用 `https://kitchen.maniac3221225477.sbs/#device-bind=<token>`，网页保存本标签页待绑定token并清地址栏；与成员邀请独立。

## 未完成门禁

公开bootstrap目前只有数据库串行的全局每天100台新设备上限，已足够限制开发误循环，但不是完善的防滥用：攻击者可耗尽额度，没有制造凭据证明正版。**公开产品发布前必须增加网关/IP限流、未绑定记录回收及制造凭据策略**。数据库上限不限制HTTP流量、函数调用计费或伪造客户端。输入body限定2048字节，secret/token限定格式，响应no-store且不记录凭据。

撤销表示永久禁用当前设备凭据，不能通过enroll自行复活；不是“换厨房”完整流程。解除后的重配/恢复出厂需另外设计，当前UI明确叫“停用屏幕同步”。旧缓存不自动清理，已签发URL最长120秒有效。

固件已接入：CA/hostname/SNTP；流式下载<=1MiB；不自动跨域跳转；下载时不携带设备secret；释放TLS后使用esp_new_jpeg逐block完整解码验证1024x600（EOI/输出行数）；A/B文件槽+NVS指针，候选失败保旧图；挂载失败不format；日志不打印凭据。首次身份保存在cloud_device namespace的单一blob，照片有效指针在cloud_photo namespace。旧current.jpg保留为迁移前缓存。UI设置新增Cloud / Bind screen入口（英文以复用已有字体），首次打开才生成绑定码；断网/校时未完成会等待并重试。

mbedTLS分配策略从internal切为external：当前internal largest约14.8KB不足默认TLS16KB输入，PSRAM承载TLS，网络请求结束先释放连接再JPEG校验；TLS峰值仍须实测，不能用PSRAM总余量代替。代码不改变PCLK/framebuffer/bounce/PSRAM频率或分区。已实现重试间隔15秒，未做大规模生产调度；首次注册与QR/图片同步必须云端部署与QA通过后再烧录。
