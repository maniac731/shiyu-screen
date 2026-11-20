# 照片上传契约

状态：SQL migration 与隔离测试已实现，尚未部署；数据库执行测试及真实 Storage API 测试未运行。

## 前端接口

- 私有 bucket：`kitchen-photos`，仅 JPEG，单文件最多 `1,048,576` 字节。
- 前端生成 `photoId = crypto.randomUUID()`，路径为 `${householdId}/${photoId}.jpg`。
- 首版在浏览器将图片裁为 1024×600 JPEG，预览确认后上传；本轮不对接屏幕。
- `storage.from('kitchen-photos').upload(path, blob, {contentType:'image/jpeg', upsert:false})` 成功后，再向 `public.photos` insert `{id, household_id, object_path, name}`；created_by 默认当前登录用户。
- 表字段：`id, household_id, object_path, name, created_at, created_by`。无 UPDATE 权限；按 `created_at` 排序；当前没有产品数量上限。
- 列表只读本厨房记录；预览可用 Storage `download` 创建本地 object URL，或短时 signed URL。禁止生成公开 URL。
- 上传成功但 metadata insert 失败时，调用 Storage remove 清理刚上传的路径；若清理失败，明确提示并保留重试入口，不能宣称上传成功。
- 删除顺序：Storage API `remove([path])` → 删除 photos 记录。必须检查错误和实际结果；metadata 删除后重新查询确认。Storage 已不存在时允许重试 metadata 删除。

## 权限与限制

所有当前厨房成员平权读、上传和删除；created_by 仅用于归属记录，不限制其他成员管理。客户端无法创建其他厨房的 metadata，也无法伪造路径和创建者。记录插入前检查 Storage 对象存在，MIME 为 image/jpeg，metadata size 为 1..1MiB。

Storage 路径只允许两段 UUID 和 `.jpg`，禁止嵌套路径；不提供 UPDATE/upsert。除许可策略外，增加该 bucket 的 restrictive fence，防止其他功能宽泛策略意外开放本厨房照片。管理员/service_role 仍有管理权限，不属于客户端威胁模型。

服务器的 bucket 校验 MIME 和大小，不会仅靠 SQL 解析 JPEG 字节或证明实际分辨率。1024×600 在首版由浏览器生成；未来设备同步前应由可信服务解码验证并生成设备资源，不能把客户端或可伪造的 MIME/metadata 当作二进制验证。

Storage 与 photos 不在同一个数据库事务中。两个成员同时上传/删除可能产生短暂缺记录或孤立对象；首版采用唯一随机路径、补偿清理、重试和明确错误。后续应做服务端孤立对象回收，但本轮不自动删除文件。已签发的 URL 在到期前仍可能有效；移除成员并非即时撤销已下载文件。

## 验证

先应用 migration `20261120000300_photos.sql`。它若发现同名 bucket 配置不同，会报错并回滚，不会改写该 bucket。

一次性本地 Supabase 数据库中执行：

```sh
psql "$SHIYU_TEST_DATABASE_URL" -v ON_ERROR_STOP=1 -f supabase/tests/photos.sql
```

测试合成两厨房和普通成员，检查路径格式、跨厨房读写隔离、无对象不能插记录、禁止覆盖、文件先删再删记录及匿名拒绝；最终回滚。测试里的 storage.objects 行仅为模拟元数据，不能代替真实上传下载或 MIME/大小限制测试。生产删除文件必须使用 Storage API，禁止直接删除 storage.objects 行。

云端部署后人工验收：同厨房两账号上传/看图/删除；另一厨房读写被拒；匿名访问失败；超过1MiB或非JPEG上传拒绝；失败补偿可重试；离开页面释放 object URL；刷新仍可见。尚未运行的项目保持未验证。

参考：[Storage RLS](https://supabase.com/docs/guides/storage/security/access-control)、[Storage schema](https://supabase.com/docs/guides/storage/schema/design)、[删除对象](https://supabase.com/docs/guides/storage/management/delete-objects)。
