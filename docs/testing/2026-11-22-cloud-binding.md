# 设备绑定与云照片同步：阶段记录

目标：shiyu_idf 首次二维码绑定、持久化设备身份、同步最新一张云照片。

## 已完成证据

- 硬件基线保持 30MHz、DOUBLE_PARTIAL mode 6、2 framebuffer、20 行 bounce、120MHz PSRAM；原分区未改。
- 固件最终构建由单一开发 Agent 完成：ESP-IDF 5.5.6，bin `0x2267d0`，APP 余约28%，DIRAM 138991B。新增设备worker、QR、TLS、A/B缓存、完整分块JPEG校验。
- 独立QA已复核两项修复：坏缓存启动清known ID重新下载；过期或生成失败隐藏QR。
- Deno check 两个 Edge Functions 通过。网页构建及6项已有逻辑测试通过；这些测试未覆盖真实扫码绑定。
- 隔离 PGlite PostgreSQL（模拟Auth/Storage）执行001–004迁移及households/photos/devices测试通过，不代表真实Storage HTTP或并发通过。
- 远程只读核对001–003函数源、RLS、授权和私有照片桶与本地一致；已补登记手动执行历史。
- `db push --dry-run`只列004；随后004已部署，device-api/device-owner已通过CLI use-api部署到 `lkewydexsjjntydpbzri`。

## 当前阻断

本机请求 `https://lkewydexsjjntydpbzri.supabase.co` 的Auth接口遇到ECONNRESET。真实临时账号/设备E2E脚本在创建首个测试账号前失败，未验证权限、过期/重放、并发claim或撤销的真实HTTP链路；临时fixture清理完成。可信DoH解析IP对照且保持TLS校验也连接重置。管理API和PostgreSQL连接成功不能替代设备域名可达。

用户明确要求先执行真机烧录验证后，本次固件已通过 USB 烧录，2254800 字节应用及原分区表写入后均 Hash verified；未擦除照片分区。串口证据：`firmware/shiyu_idf/build/log/idf_py_stdout_output_10116`。网页及接口源码已提交并推送 `063ffb2`，Cloudflare 自动部署结果未独立核对。

烧录后串口验证：16MB Flash、8MB PSRAM/120MHz 初始化成功；界面启动，Wi-Fi 自动连接并取得 IP。两次云 API 请求均 TLS 握手失败 `-0x0050`（本地 mbedTLS 定义为连接重置），尚未完成 enroll/绑定/下载。请求前后内部可用约24–25KB，PSRAM约3.7MB，未出现分配失败或此次短时观察内重启。不能据此证明长期稳定或确定重置来源；显示与触摸仍需人工验收。

## 状态

- implemented：是（最小链路代码）
- compiled：是
- 云端设备SQL/函数 deployed：是；真实HTTP验收未通过
- flashed：是（写入校验通过）
- device-tested：部分（启动和Wi-Fi通过；云TLS连接失败，绑定与照片同步未验证）
- accepted：否

## 安卓热点复测

切换安卓手机热点后，串口确认自动连接并获取 IP。启动约52秒出现 `esp-x509-crt-bundle: Certificate validated`，首个 enroll 请求成功返回并进入 status 请求（代码仅在 enroll 成功解析200响应后进入此步骤）；约60秒 status 连接也通过证书校验。相同固件无需修改即可通过热点建立安全连接。此前店内网络连接重置的具体来源仍未确定，不能据此保证所有网络可用。扫码绑定、照片下载和断电恢复仍待验收。

下一步验收扫码绑定、照片下载和断电恢复。不得通过跳过证书或改变显示基线绕过网络阻断。
