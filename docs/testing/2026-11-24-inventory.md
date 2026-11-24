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
