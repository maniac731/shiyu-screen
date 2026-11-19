# Codex 混合多 Agent 工作流

项目级 Agent 位于 `.codex/agents/`，硬件闭环技能位于 `.agents/skills/shiyu-screen-hardware-cycle/`。

## 自然语言入口

用户不需要使用固定提示词、点名 Agent 或选择流程，可以直接说“我想达到什么效果”“设备出现了什么现象”“我怀疑原因是什么”或“我想理解什么”。

当前主 Agent先在内部判断最小充分流程。目标清楚时不调用产品 Agent，也不启动自定义协调 Agent；只有需求模糊、存在产品取舍或验收不清时才调用产品 Agent。

例如用户说“深色页面还是会抖，我怀疑和内存有关”，主 Agent先区分抖动事实和内存归因，只调用最能补齐当前证据的 QA 或方案研究 Agent；证据确实指向跨层资源问题时才升级到架构，而不是一次启动所有角色。

只有涉及烧录、分区、擦除、数据丢失、不可逆迁移或产品取舍时，流程才必须停下来交回用户决定。

路由验收：用户只提供自然语言；主 Agent选择直接、标准或深度模式；没有明确价值的角色不调用；只有一个开发 Agent写入；验证强度与风险相称；未提前声称烧录、真机或接受完成。

## 方案研究门禁

以下任务才调用 `shiyu_screen_solution_researcher`：引入新框架或关键组件、成熟方案未知、改变显示/存储/网络架构、重新分配 Flash/SRAM/PSRAM、改变任务并发模型、从 Arduino 迁移到 ESP-IDF，或设计需要长期演进的新能力。

研究阶段不写代码。只有在候选方案具备来源、版本适配、资源预算、维护成本、回退路径和验证方法后，主协调者才能放行架构细化与开发。

自定义 Agent 遵循 OpenAI 官方规范：每个 TOML 包含 `name`、`description`、`developer_instructions`；Agent继承主会话权限，因此“只读”是行为约束。项目不固定模型，避免账户不可用。

规范：<https://learn.chatgpt.com/docs/agent-configuration/subagents>
