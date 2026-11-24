# 食愈手机网页端

手机优先的 Vite + TypeScript 页面，使用官方 Supabase JS SDK。目前实现邮箱验证码登录、厨房创建、成员列表、创建者分享邀请链接与移除成员、接收者预览并主动接受邀请，以及照片选择、裁切预览、上传、列表和删除。食材和设备同步尚未接入。照片功能必须先应用对应照片迁移；本地代码完成不代表云端已部署或真机验收。

- `src/`：后续页面与客户端逻辑。
- `.env.example`：本地配置模板，复制为 `.env.local` 后填写 publishable key。

任何进入浏览器的环境变量都视为公开信息。禁止填入数据库密码、secret key 或 service_role key。发布前须通过 RLS 验证跨家庭隔离，不能依靠前端过滤保障权限。

## 本地运行

## Cloudflare Workers 静态部署

沿用 `shiyu-screen` Worker。Git 构建 Root directory 填 `web`，Build command 填 `npm run build`，Deploy command 填 `npx wrangler deploy`。`wrangler.jsonc` 只发布 `dist/`，不需要服务端入口代码。

在 Cloudflare **构建变量**中设置 `VITE_SUPABASE_URL` 和 `VITE_SUPABASE_PUBLISHABLE_KEY`，以及 `NODE_VERSION=22`。运行时 Worker 变量不能代替 Vite 构建变量。使用仓库 lockfile 安装依赖，不上传 `.env.local`。这些 VITE 变量会被打包进公开网页，仅可使用 publishable key。

部署后将 Supabase Site URL 更新为实际 HTTPS 网页地址（需要保留本地测试时将本地地址加入对应允许列表），测试邮箱验证码、厨房、邀请与照片。硬件绑定/同步仍未实现；不得将网页部署成功记录为硬件联网同步通过。

### 本地命令

需要 Node.js 20.19+。将 `.env.example` 复制为 `.env.local`，填写 `VITE_SUPABASE_PUBLISHABLE_KEY`（仅公开 publishable key），然后：

```sh
npm ci
npm run dev
npm run check
npm test
npm run build
```

开发服务默认只监听本机。手机真机测试需要可访问的 HTTPS 部署或受控局域网服务；正式站点不要使用 Vite 开发服务器。缺少配置时页面会显示配置说明，不会模拟登录成功。

## Supabase 前置配置

1. 在获准的数据库环境应用 `supabase/migrations/` 中全部迁移，并运行数据库测试；网页构建通过不代表 RLS 已验收。
2. 在 Auth 启用 Email 登录，保持邮箱验证。注册/登录共用 `signInWithOtp`，验证使用 `verifyOtp({ email, token, type: 'email' })`。
3. 将 Auth 的 Magic Link 邮件模板改为展示 `{{ .Token }}` 的验证码邮件，例如 `<p>食愈登录验证码：{{ .Token }}</p>`，不要仅放 `{{ .ConfirmationURL }}`。前端不实现 magic-link 回调。
4. 验证码长度和过期时间由项目 Auth 配置决定；页面不强制写死 6 位。
5. 普通用户试用前配置自己的 SMTP、发送域名及发送限额。Supabase 默认邮件服务仅适合项目团队地址测试，不能视为已具备生产发送能力。
6. 设置实际站点 URL，检查邮件到达率及垃圾箱；限制发送频率、防滥用措施需在公开发布前验证。前端防重复点击不能替代服务端限制。

邀请链接把 64 位令牌放在 `#invite=` 中。页面先保存至本标签页 sessionStorage，再清理地址栏；未登录先验证邮箱，之后只预览厨房名称与有效期，点击确认才兑换。退出登录不会删除本标签页待处理邀请，可点击“放弃此邀请”清除。生成链接不写日志，不接第三方分析；凭证应只发给预期成员。共享设备需要手动退出，登录会话由 SDK 保存在浏览器。

成员显示真实数据库 UUID（成员编号），不伪造姓名；后续显示名设计单独实施。创建者权限最终由 RPC/RLS 校验，网页隐藏按钮不构成授权。

## 验证边界

纯逻辑测试涵盖令牌格式、分享 URL、邀请过期和已有厨房判定。真实邮件、数据库权限/并发、邀请端到端和手机浏览器实测需具备云端配置后执行；未部署前不可标记这些项目通过。

参考：[Supabase 邮箱 OTP](https://supabase.com/docs/guides/auth/auth-email-passwordless)、[SMTP](https://supabase.com/docs/guides/auth/auth-smtp)。

## 照片上传

厨房内所有成员均可选择 JPEG/PNG/WebP（原文件最多20MB），按浏览器提供的图片方向居中裁切为1024×600，并在预览后明确确认上传。HEIC暂不支持。解码后超过4800万像素或单边16000px会拒绝；解码前的浏览器内存占用仍受原图影响，低内存手机需实测。输出JPEG最多1MiB。

私有bucket为`kitchen-photos`，对象路径`householdUUID/photoUUID.jpg`；先上传文件再写`photos`。登记失败尝试删除文件，清理失败时提供重试按钮；删除照片先调用Storage API，再删除记录，失败保留记录供重试。Storage与数据库不是一个事务：关闭标签页、断网或中途退出登录可能留下未登记对象，后端仍需定期孤儿清理，不能宣称原子上传。不要用SQL直接删除storage.objects代替Storage API。

列表使用120秒签名地址；移除成员后，已发出的签名地址可能继续有效直到到期，已经下载的图片无法追回。链接过期可点击“刷新照片”。照片列表失败不会阻断成员管理。当前仅手机→云端，屏幕下载和轮播尚未实现。

本地纯逻辑测试新增居中裁切、文件类型/大小限制和路径归属。真实图片解码、EXIF方向、手机触摸、上传删除、跨家庭隔离及故障补偿必须在部署后验证。无个人照片或真实凭证写入测试。

## 厨房入口与空厨房切换

注册仅建立账号；现有代码本来就没有注册自动创建厨房。无厨房账号先选择“创建我的厨房”或“加入已有厨房”，后者说明向创建者获取邀请链接。待处理邀请仍先登录，再预览并确认。网页使用新增 `preview_household_join` / `accept_household_join` RPC，必须先应用 `20261122000100_explicit_kitchen_join.sql`。

已有厨房账号仅在自己是创建者、没有其他成员、照片记录、Storage 前缀对象和任何设备绑定记录（含撤销设备）时可明确确认切换。事务重新校验并删除旧空厨房及其旧邀请，加入目标厨房；失败保留原数据和邀请。Storage 写操作锁成员行，切换锁成员及厨房行，避免上传、成员加入、照片登记和设备绑定并发绕过校验。当前没有库存表；若未来出现 `public.inventory` 或 `public.inventory_items`，切换会暂停放行，须先补库存检查与并发保护。

2026-11-22 本地验证：网页 6 项逻辑测试及生产构建通过；PGlite 隔离数据库应用全部迁移并通过 households/photos/devices/kitchen_join 四套 SQL 测试（模拟 Auth/Storage schema）。未执行真实 Supabase 多连接并发、真实邮件或手机端验收，不以此替代部署及真机验证。

## 简化库存

登录厨房后可直接管理名称、数量、单位。同名同单位新增会累加，单位不同拒绝；编辑修改总量，0保留记录。工程首版最多100条，所有成员平权。新增有request UUID幂等，编辑/删除用版本校验防止静默覆盖；冲突保留输入，刷新后重新编辑。必须部署20261124000100迁移。位置、批次和到期提醒本阶段不实现。设备及离线缓存说明统一见 `docs/backend/inventory.md`。
