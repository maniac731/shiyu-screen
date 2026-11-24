# 真实库存闭环验收

- 目标路线：shiyu_idf；起点db3be25。
- 范围：名称、数量、单位；同名累加；手机CRUD、屏幕分页只读。位置/到期/批次不做。
- 旧轮播网络/断电/耐久矩阵按用户要求延后，仍未验证。
- 回退：已验收轮播固件 `/private/tmp/shiyu-inventory-baseline/carousel.bin`；源码起点db3be25。回退固件不删除新增库存数据，不覆盖用户已有未提交字体/文档/照片。
- 门禁：7迁移6套隔离SQL、web构建及6原测试、Deno检查、IDF编译通过。独立只读QA GO。
- 固件本地构建0x22b860，APP余28%，DIRAM141951B；inventory init栈64B，UI刷新336B，大结构与JSON放PSRAM。
- implemented / compiled：完成；云端部署/真实HTTP测试/flashed/device-tested/accepted待记录。

## 云端与真实HTTP

- 迁移预览只有20261124000100；首次直连IPv6超时，重试成功。官方已配置连接池只读复核既有迁移一致，TLS保持校验。
- 新库存迁移及device-api已部署。
- 临时合成Auth账号、独立厨房及设备真实HTTP测试通过：add、幂等重试不重复累加、同名合并、跨厨房SELECT隔离、过期编辑版本拒绝、单位冲突拒绝；设备只取绑定厨房库存、known revision unchanged、错误密钥和撤销拒绝。
- 全部临时账号/厨房/设备/库存和幂等记录已清理，未操作用户厨房内容。
- 稳定性测试仍按用户要求延后。本轮不强制断网或做异常断电测试。

## 发布与烧录

- 源码已提交推送 `ec60357`，Cloudflare生产首页确认加载库存版 `index-DObmUwVb.js`。
- 最终复编译后应用2275424字节写入并Hash verified，原分区表校验通过，无整片擦除。
- 启动：库存无旧缓存（正常首装），库存模型初始化未触发重启；主栈high-water1596，UI后内部18151B/最大块9728B、PSRAM2385220B。
- 原有照片恢复2张，环境模式约10秒轮播（仅现场观测，不扩大旧稳定性验收）。
- 正常业务联网已观察到；等待云端快照及用户手机录入验收，不主动断网、不测试异常断电。
- implemented / compiled / 云端deployed / flashed：完成；设备端新库存同步和accepted待记录。

## 正常同步前置校时修复

用户反馈联网后库存一直未同步，串口仍为Waiting for secure clock，尚未发库存请求。继承的单pool.ntp.org配置且SNTP在Wi-Fi前启动，当前网络未取得时间。保留TLS日期校验，增加ntp.aliyun.com、ntp.tencent.com与pool.ntp.org三源，并仅联网后初始化SNTP，避免未联网时提前进入退避；加校时成功日志。未进行主动断网测试。

地址依据：[阿里云公开NTP](https://help.aliyun.com/en/ecs/user-guide/alibaba-cloud-ntp-server/)、[腾讯云公开NTP](https://intl.cloud.tencent.com/document/product/213/32379?lang=en)。新配置已写defaults；独立只读QA GO，编译0x22b980，APP余28%；待重刷实际确认。

## 本轮实机与人工验收

- 校时修复版2275712字节（0x22b980）已重新写入并Hash verified，原分区表未变。
- 设备恢复库存缓存1条，主栈余量1596；库存模型/JSON用PSRAM，UI后PSRAM约2.38MB、内部约18KB，常态网络后内部约21KB。
- 正常联网约3.749秒，SNTP约19.127秒记录Secure clock synchronized；48.317秒绑定确认，56.305秒库存请求结束，后续照片同步成功。只修复正常业务前置，不声称完成网络稳定性测试。
- 云端只读统计证实真实绑定厨房有1条库存，未读取/记录用户食材名称。设备缓存也恢复1条；用户最终明确回复“已显示，内容一致”。
- accepted：本轮手机新增真实食材→云端→屏幕名称/数量/单位一致已接受。device-tested：正常启动、真实库存读取及原照片轮播现场运行；不扩大为全部CRUD、100条上限、断网、故障断电或耐久验收。
- 后续待测：手机成功编辑/删除到屏幕的完整人工流程，100条分页与最大快照、故障保存/缓存回退及多人真实并发。SQL与合成HTTP覆盖项保留独立证据，不冒充这些人工场景已通过。
