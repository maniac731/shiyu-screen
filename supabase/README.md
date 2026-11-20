# 食愈后端

云端项目 reference：`lkewydexsjjntydpbzri`。

本目录保存可版本化的数据库与服务端代码；真实数据、照片和密钥不进入 Git。

## 目录

- `migrations/`：按时间排序的 SQL 迁移，涵盖表、索引、RLS 和 Storage 权限。
- `functions/`：需要服务端权限的 Edge Functions，例如设备绑定、同步授权。
- `tests/`：家庭隔离、成员权限、设备权限和迁移测试。

已编写厨房、成员、私有邀请表及四个 RPC 的首份 SQL 迁移和集成测试；尚未运行数据库执行测试或部署云端。尚未创建 bucket 或设备授权。
本地 CLI 配置待后端实施时通过所选 Supabase CLI 版本生成；当前未链接或部署远程项目。

## 实施约束

每次云端变更先保存 migration 并在本地验证，再部署。所有家庭业务表必须启用 RLS；Storage 使用私有 bucket 和受限访问。浏览器只使用 publishable key，管理员 secret/service_role key 仅限受信任服务端环境。屏幕不得持有管理员密钥。

第一版范围与验收见 `../docs/backend/README.md`。
