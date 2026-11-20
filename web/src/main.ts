import { createClient, type Session } from '@supabase/supabase-js';
import { parseInvite, buildInviteUrl, invitationState, INVITE_STORAGE_KEY } from './invitations.mjs';
import './style.css';
import { mountPhotos } from './photos';

const app = document.querySelector<HTMLDivElement>('#app')!;
const url = import.meta.env.VITE_SUPABASE_URL?.trim();
const key = import.meta.env.VITE_SUPABASE_PUBLISHABLE_KEY?.trim();
const tokenFromUrl = parseInvite(location.hash);
let pendingInvite: string | null = tokenFromUrl;
try {
  if (tokenFromUrl) sessionStorage.setItem(INVITE_STORAGE_KEY, tokenFromUrl);
  else pendingInvite = parseInvite(`#invite=${sessionStorage.getItem(INVITE_STORAGE_KEY) ?? ''}`);
} catch { /* The in-memory invitation works when browser storage is unavailable. */ }
if (location.hash.includes('invite=')) history.replaceState(null, '', location.pathname + location.search);

function el<K extends keyof HTMLElementTagNameMap>(tag: K, text?: string, className?: string) {
  const node = document.createElement(tag);
  if (text) node.textContent = text;
  if (className) node.className = className;
  return node;
}
const masthead = el('header');
masthead.append(el('p', '食愈', 'brand'), el('h1', '把厨房留给生活'), el('p', '和家人一起，把每一份食材照顾好。', 'muted'));
app.append(masthead);
const notice = el('p', '', 'notice'); notice.setAttribute('role', 'status'); notice.setAttribute('aria-live', 'polite');
const content = el('section', '', 'card'); app.append(notice, content);
function message(text = '') { notice.textContent = text; }
function action(text: string, handler: () => void, secondary = false) {
  const button = el('button', text, secondary ? 'secondary' : '');
  button.type = 'button'; button.addEventListener('click', handler); return button;
}
function field(form: HTMLElement, name: string, label: string, type = 'text') {
  const input = el('input'); input.id = name; input.name = name; input.type = type; input.required = true;
  const caption = el('label', label); caption.htmlFor = name; form.append(caption, input); return input;
}
function clearInvite() { pendingInvite = null; try { sessionStorage.removeItem(INVITE_STORAGE_KEY); } catch { /* optional storage */ } }
function readableError(error: unknown) {
  const code = (error as {code?: string})?.code;
  if (code === 'otp_expired') return '验证码无效或已过期，请重新获取。';
  if (code === 'over_email_send_rate_limit' || code === 'over_request_rate_limit') return '发送过于频繁，请稍后重试。';
  if (code === 'email_address_not_authorized') return '邮件服务尚未开放此邮箱，请联系维护者配置邮件发送服务。';
  if (code === '22023') return '邀请不可用：可能已过期、已使用，或当前账号已有厨房。';
  if (code === '42501') return '当前账号没有此操作权限。';
  if (code === 'PGRST202' || code === '42P01') return '后端接口尚未就绪，请先完成数据库迁移。';
  return '操作未完成，请检查网络后重试。';
}
if (!url || !key || !/^https?:\/\//.test(url) || key.startsWith('sb_secret_')) {
  content.append(el('h2', '连接食愈后端'), el('p', '本地配置尚未完成，请按照 web/README.md 设置公开项目地址和 publishable key。'));
} else {
  const client = createClient(url, key, { auth: { detectSessionInUrl: false, persistSession: true, autoRefreshToken: true } });
  let session: Session | null = null;
  let generation = 0;
  let busy = false;
  let sentEmail = '';
  let disposePhotos: (() => void) | undefined;

  async function run(button: HTMLButtonElement, task: () => Promise<void>) {
    if (busy) return;
    busy = true; button.disabled = true; button.setAttribute('aria-busy', 'true'); message('正在处理…');
    try { await task(); } catch (error) { message(readableError(error)); }
    finally { busy = false; button.disabled = false; button.removeAttribute('aria-busy'); }
  }
  function login() {
    content.replaceChildren(el('h2', pendingInvite ? '登录后加入家人的厨房' : '欢迎回到食愈'));
    const form = el('form');
    const email = field(form, 'email', '邮箱', 'email'); email.autocomplete = 'email'; email.value = sentEmail;
    const send = action('获取验证码', () => void run(send, async () => {
      if (!form.reportValidity()) { message(); return; }
      const submittedEmail = email.value.trim();
      const { error } = await client.auth.signInWithOtp({ email: submittedEmail, options: { shouldCreateUser: true } });
      if (error) throw error;
      sentEmail = submittedEmail; login(); message('验证码已发送，请查看邮箱。');
    }));
    form.addEventListener('submit', event => { event.preventDefault(); send.click(); });
    form.append(send); content.append(form);
    if (sentEmail) {
      const verifyForm = el('form'); const code = field(verifyForm, 'code', '邮箱验证码');
      code.autocomplete = 'one-time-code'; code.inputMode = 'numeric';
      const verify = action('登录', () => void run(verify, async () => {
        if (!verifyForm.reportValidity()) { message(); return; }
        const { error } = await client.auth.verifyOtp({ email: sentEmail, token: code.value.trim(), type: 'email' });
        if (error) throw error; message('登录成功。');
      }));
      verifyForm.append(verify); verifyForm.addEventListener('submit', event => { event.preventDefault(); verify.click(); });
      content.append(verifyForm);
    }
    content.append(el('p', '首次验证邮箱会自动建立账号。无需设置密码。', 'muted'));
  }
  async function dashboard() {
    disposePhotos?.(); disposePhotos = undefined;
    const currentGeneration = ++generation;
    const currentSession = session;
    if (!currentSession) { login(); return; }
    content.replaceChildren(el('p', '正在读取厨房…'));
    const { data: households, error } = await client.from('households').select('id,name,owner_id');
    if (generation !== currentGeneration) return;
    if (error) { content.replaceChildren(el('h2', '暂时无法读取厨房'), action('重试', () => void dashboard())); message(readableError(error)); return; }
    const kitchen = households?.[0];
    content.replaceChildren(el('h2', kitchen?.name ?? '建立你的家庭厨房'), el('p', currentSession.user.email ?? '已登录', 'muted'));
    const signOut = action('退出登录', () => void run(signOut, async () => {
      const { error: signOutError } = await client.auth.signOut({ scope: 'local' });
      if (signOutError) throw signOutError; message('已退出登录；待接受的邀请在本标签页保留。');
    }), true);
    content.append(signOut);
    if (pendingInvite) {
      const invite = el('section', '', 'invite'); content.append(invite);
      if (kitchen) {
        invite.append(el('h3', '当前账号已经属于一个厨房'), el('p', '每个账号只能加入一个厨房。可以退出登录，更换账号接受邀请。'));
      } else {
        const invitationToken = pendingInvite;
        const { data, error: previewError } = await client.rpc('preview_invitation', { p_code: invitationToken });
        if (generation !== currentGeneration || pendingInvite !== invitationToken) return;
        const preview = data?.[0];
        if (previewError || invitationState(false, preview) !== 'available') {
          invite.append(el('h3', '邀请暂不可用'), el('p', '请向家人获取新的邀请链接。'));
        } else {
          invite.append(el('h3', `加入「${preview.household_name}」`), el('p', `有效期至 ${new Date(preview.expires_at).toLocaleString('zh-CN')}`));
          const accept = action('确认加入厨房', () => void run(accept, async () => {
            const { error: acceptError } = await client.rpc('redeem_invitation', { p_code: invitationToken });
            if (acceptError) throw acceptError;
            clearInvite(); message('已加入厨房。'); await dashboard();
          })); invite.append(accept);
        }
      }
      invite.append(action('放弃此邀请', () => { if (busy) return; clearInvite(); message(); void dashboard(); }, true));
      return;
    }
    if (!kitchen) {
      const form = el('form'); const name = field(form, 'kitchen-name', '厨房名称'); name.maxLength = 80;
      const create = action('创建厨房', () => void run(create, async () => {
        if (!form.reportValidity() || !name.value.trim()) { message('请填写厨房名称。'); return; }
        const { error: createError } = await client.rpc('create_household', { p_name: name.value.trim() });
        if (createError) throw createError; message('厨房已创建。'); await dashboard();
      })); form.append(create); form.addEventListener('submit', event => { event.preventDefault(); create.click(); }); content.append(form); return;
    }
    const photos = el('section'); content.append(photos);
    disposePhotos = mountPhotos(photos, client, kitchen.id, currentSession.user.id,
      () => generation === currentGeneration && session?.user.id === currentSession.user.id);
    content.append(el('p', '食材管理和屏幕同步将在后续接入。', 'muted'));
    if (kitchen.owner_id === currentSession.user.id) {
      const inviteButton = action('邀请家人加入', () => void run(inviteButton, async () => {
        const { data: token, error: inviteError } = await client.rpc('create_invitation');
        if (inviteError) throw inviteError;
        const shareUrl = buildInviteUrl(location.href, token);
        let copied = false;
        try { await navigator.clipboard.writeText(shareUrl); copied = true; } catch { /* manual selection below */ }
        const box = el('div', '', 'invite'); box.append(el('p', '邀请一次有效、24 小时过期，请只分享给预期成员。'));
        const label = el('label', '邀请链接'); const share = el('textarea'); share.id = 'share-link'; label.htmlFor = share.id;
        share.readOnly = true; share.value = shareUrl; share.rows = 3; share.addEventListener('focus', () => share.select());
        box.append(label, share); content.append(box); inviteButton.disabled = true;
        message(copied ? '邀请链接已复制。' : '请选中下方链接并复制。');
      })); content.append(inviteButton);
    }
    const { data: members, error: memberError } = await client.from('household_members').select('user_id,joined_at').order('joined_at');
    if (generation !== currentGeneration) return;
    if (memberError) { message(readableError(memberError)); return; }
    content.append(el('h3', '厨房成员'));
    const list = el('ul');
    for (const member of members ?? []) {
      const row = el('li');
      row.append(el('span', `成员编号 ${member.user_id}${member.user_id === currentSession.user.id ? '（我）' : ''}`));
      if (kitchen.owner_id === currentSession.user.id && member.user_id !== currentSession.user.id) {
        const remove = action('移除成员', () => {
          if (!confirm('移除后，该成员将无法继续访问厨房。确定移除吗？')) return;
          void run(remove, async () => {
            const { error: removeError } = await client.rpc('remove_member', { p_user_id: member.user_id });
            if (removeError) throw removeError; message('成员已移除。'); await dashboard();
          });
        }, true); row.append(remove);
      }
      list.append(row);
    } content.append(list);
  }
  client.auth.onAuthStateChange((_event, next) => {
    // Defer async DB calls until Supabase finishes its auth callback lock.
    if (session?.user.id === next?.user.id && session !== null) { session = next; return; }
    session = next; generation++; setTimeout(() => void dashboard(), 0);
  });
  void client.auth.getSession().then(({ data, error }) => {
    if (error) message(readableError(error));
    session = data.session; void dashboard();
  });
}
