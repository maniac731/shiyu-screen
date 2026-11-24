import type { SupabaseClient } from '@supabase/supabase-js';

type Item = { id: string; name: string; quantity: number; unit: string; version: number; expires_on: string | null };
function node<K extends keyof HTMLElementTagNameMap>(tag: K, text = '') {
  const el = document.createElement(tag); el.textContent = text; return el;
}
function button(text: string, action: () => void) {
  const el = node('button', text); el.type = 'button'; el.addEventListener('click', action); return el;
}
function errorText(error: unknown) {
  const code = (error as { code?: string })?.code;
  if (code === 'P1001') return '这条库存已被修改或删除。你的输入仍保留，请刷新核对后重新编辑。';
  if (code === 'P1002') return '已有同名食材，但单位不同。请沿用现有单位，或编辑原记录；不会自动换算。';
  if (code === 'P1003') return '首版最多保存 100 条库存记录，请先整理现有库存。';
  if (code === '23505') return '已有同名、同单位和同到期日的批次。新增时可累加，编辑不能覆盖另一批次。';
  if (code === '42501') return '当前账号已无权访问此厨房，请重新登录确认。';
  if (code === 'PGRST202' || code === '42P01') return '库存接口尚未部署，请稍后重试。';
  if (code === '22023') return '请检查名称、数量和单位；数量最多 3 位小数。';
  return '暂时无法完成操作。请重试；同一笔新增重试不会重复累加。';
}
export function mountInventory(host: HTMLElement, client: SupabaseClient, household: string, current: () => boolean) {
  let alive = true, busy = false, sequence = 0;
  let requestPayload = '', requestId = '';
  const valid = () => alive && current();
  const notice = node('p'); notice.setAttribute('role', 'status'); notice.setAttribute('aria-live', 'polite');
  const list = node('div'), editor = node('section'), add = node('form');
  function fields(form: HTMLElement, prefix: string, value?: Item) {
    const result: Record<string, HTMLInputElement> = {};
    for (const [key, title] of [['name', '食材名称'], ['quantity', '数量'], ['unit', '单位'], ['expires_on', '到期日期（可选）']]) {
      const input = node('input'); input.id = `${prefix}-${key}`; input.required = key !== 'expires_on';
      input.type = key === 'quantity' ? 'number' : key === 'expires_on' ? 'date' : 'text';
      if (key === 'quantity') { input.min = value ? '0' : '0.001'; input.max = '999999.999'; input.step = '0.001'; input.inputMode = 'decimal'; }
      else if (key !== 'expires_on') input.maxLength = key === 'name' ? 40 : 8;
      input.value = value ? String(value[key as keyof Item] ?? '') : key === 'quantity' ? '1' : '';
      if (key === 'unit') input.placeholder = '例如：个、克、盒';
      const label = node('label', title); label.htmlFor = input.id;
      form.append(label, input); result[key] = input;
    }
    return result;
  }
  const f = fields(add, 'inventory-add');
  function payload(inputs: Record<string, HTMLInputElement>) {
    const name = inputs.name.value.trim(), unit = inputs.unit.value.trim(), raw = inputs.quantity.value;
    const quantity = Number(raw);
    if (!name || name.length > 40 || new TextEncoder().encode(name).length > 120 || /[\u0000-\u001f\u007f]/.test(name) ||
        !unit || unit.length > 8 || new TextEncoder().encode(unit).length > 24 || /[\u0000-\u001f\u007f]/.test(unit) ||
        !/^\d+(\.\d{1,3})?$/.test(raw) || !Number.isFinite(quantity) || quantity < 0 || quantity > 999999.999) {
      throw { code: '22023' };
    }
    const expiry = inputs.expires_on.value;
    if (expiry && !/^[0-9]{4}-[0-9]{2}-[0-9]{2}$/.test(expiry)) throw { code: '22023' };
    return { p_name: name, p_quantity: quantity, p_unit: unit, p_expires_on: expiry || null };
  }
  async function run(task: () => Promise<void>) {
    if (!valid() || busy) return;
    busy = true; notice.textContent = '正在保存…';
    host.querySelectorAll<HTMLButtonElement>('button').forEach(b => b.disabled = true);
    try { await task(); } catch (error) { if (valid()) notice.textContent = errorText(error); }
    finally { busy = false; if (valid()) host.querySelectorAll<HTMLButtonElement>('button').forEach(b => b.disabled = false); }
  }
  async function refresh() {
    const turn = ++sequence;
    const { data, error } = await client.from('inventory_items').select('id,name,quantity,unit,version,expires_on').eq('household_id', household).order('expires_on', { nullsFirst: false }).order('name_key');
    if (!valid() || turn !== sequence) return;
    if (error) { notice.textContent = errorText(error); return; }
    list.replaceChildren();
    const items = (data ?? []) as Item[];
    list.append(node('p', items.length ? `共 ${items.length} 条库存记录` : '厨房库存为空，添加第一种食材吧。'));
    for (const item of items) {
      const row = node('article'); row.className = 'inventory-row';
      row.append(node('strong', item.name), node('p', `${Number(item.quantity)} ${item.unit}`), node('p', item.expires_on ? `到期：${item.expires_on}` : '未填写到期日期'), button('编辑', () => edit(item)), button('删除', () => {
        if (!confirm(`确定删除「${item.name}」这条库存吗？`)) return;
        void run(async () => {
          const { error: problem } = await client.rpc('inventory_write', { p_action: 'delete', p_item: item.id, p_version: item.version });
          if (problem) throw problem;
          if (!valid()) return;
          editor.replaceChildren(); await refresh(); notice.textContent = '已删除。';
        });
      })); list.append(row);
    }
  }
  function edit(item: Item) {
    if (!valid() || busy || (editor.childElementCount && !confirm('切换编辑会放弃未保存的输入，继续吗？'))) return;
    editor.replaceChildren(node('h4', `编辑「${item.name}」`));
    const form = node('form'), inputs = fields(form, 'inventory-edit', item);
    form.append(node('p', '这里填写当前批次数量。更改单位不会自动换算，请同时核对数量。'));
    const save = node('button', '保存修改'); save.type = 'submit';
    form.append(save, button('取消编辑', () => { if (!busy) editor.replaceChildren(); }));
    form.addEventListener('submit', event => {
      event.preventDefault(); if (!form.reportValidity()) return;
      void run(async () => {
        const args = payload(inputs);
        const { error } = await client.rpc('inventory_write', { p_action: 'update', ...args, p_item: item.id, p_version: item.version });
        if (error) throw error;
        if (!valid()) return;
        editor.replaceChildren(); await refresh(); notice.textContent = '库存批次已更新。';
      });
    }); editor.append(form); editor.scrollIntoView({ block: 'nearest' });
  }
  const submit = node('button', '新增／累加库存'); submit.type = 'submit'; add.append(submit);
  add.addEventListener('submit', event => {
    event.preventDefault(); if (!add.reportValidity()) return;
    void run(async () => {
      const args = payload(f);
      if (args.p_quantity <= 0) throw { code: '22023' };
      const signature = JSON.stringify({ ...args, p_name: args.p_name.toLowerCase() });
      if (signature !== requestPayload || !requestId) { requestPayload = signature; requestId = crypto.randomUUID(); }
      const { error } = await client.rpc('inventory_write', { p_action: 'add', ...args, p_request: requestId });
      if (error) throw error;
      if (!valid()) return;
      requestId = ''; requestPayload = ''; f.name.value = ''; f.quantity.value = '1'; f.expires_on.value = '';
      await refresh(); notice.textContent = '库存已保存，同名、同单位、同到期日的批次已累加。';
    });
  });
  host.append(node('h3', '厨房库存'), node('p', '同名、同单位、同到期日合并；不同日期分批，无日期也单独合并。最多 100 条批次。屏幕联网后会定期同步。'),
    notice, add, button('刷新库存', () => { if (!busy) void refresh(); }), editor, list);
  void refresh();
  const timer = setInterval(() => { if (valid() && !busy && !document.hidden) void refresh(); }, 30000);
  return () => { alive = false; sequence++; clearInterval(timer); };
}
