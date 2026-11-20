import type { SupabaseClient } from '@supabase/supabase-js';
import './photos.css';
import { coverCrop, photoPath, validateSource, PHOTO_WIDTH, PHOTO_HEIGHT, MAX_JPEG_BYTES } from './photo-rules.mjs';

const BUCKET = 'kitchen-photos';
const URL_TTL = 120;
type Photo = { id: string; household_id: string; object_path: string; name: string; created_at: string };

function node<K extends keyof HTMLElementTagNameMap>(tag: K, text?: string) {
  const item = document.createElement(tag);
  if (text) item.textContent = text;
  return item;
}
function button(text: string, fn: () => void) {
  const item = node('button', text); item.type = 'button'; item.addEventListener('click', fn); return item;
}

/** One mounted kitchen owns one controller. Dispose on every auth/page transition. */
export function mountPhotos(host: HTMLElement, client: SupabaseClient, householdId: string, userId: string, isCurrent: () => boolean) {
  let disposed = false, busy = false, selectionVersion = 0, loadVersion = 0;
  let previewUrl = '', selected: { blob: Blob; name: string } | null = null;
  const active = () => !disposed && isCurrent() && host.isConnected;
  const status = node('p'); status.className = 'notice'; status.setAttribute('role', 'status'); status.setAttribute('aria-live', 'polite');
  const say = (text = '') => { if (active()) status.textContent = text; };
  const heading = node('h3', '照片');
  const help = node('p', '选择照片后先预览裁切效果，再确认上传。照片将保存为 1024 × 600 JPEG，供后续屏幕同步使用。'); help.className = 'muted';
  const input = node('input'); input.type = 'file'; input.accept = 'image/jpeg,image/png,image/webp'; input.id = 'photo-file';
  const label = node('label', '从相册选择照片'); label.htmlFor = input.id;
  const preview = node('div'); preview.className = 'photo-preview';
  const gallery = node('div'); gallery.className = 'photo-gallery';
  const refresh = button('刷新照片', () => { if (!busy) void load(); }); refresh.className = 'secondary';
  host.append(heading, help, label, input, preview, status, refresh, gallery);
  function clearPreview() {
    selected = null;
    if (previewUrl) URL.revokeObjectURL(previewUrl);
    previewUrl = ''; preview.replaceChildren(); input.value = '';
  }
  async function operation(fn: () => Promise<void>) {
    if (busy || !active()) return;
    busy = true; input.disabled = true; refresh.disabled = true;
    try { await fn(); } catch (error) { say(error instanceof Error ? error.message : '照片操作失败，请重试。'); }
    finally { busy = false; if (active()) { input.disabled = false; refresh.disabled = false; } }
  }
  async function load() {
    const version = ++loadVersion;
    const { data, error } = await client.from('photos').select('id,household_id,object_path,name,created_at').eq('household_id', householdId).order('created_at', { ascending: false });
    if (!active() || version !== loadVersion) return;
    if (error) { say('照片列表暂不可用，请确认照片迁移已部署后重试。'); return; }
    gallery.replaceChildren();
    if (!data?.length) gallery.append(node('p', '还没有照片，先上传一张吧。'));
    for (const photo of (data ?? []) as Photo[]) {
      const card = node('article'); card.className = 'photo-card';
      const title = node('p', photo.name); card.append(title);
      const { data: signed, error: signError } = await client.storage.from(BUCKET).createSignedUrl(photo.object_path, URL_TTL);
      if (!active() || version !== loadVersion) return;
      if (signError || !signed) card.append(node('p', '预览不可用；文件可能已删除，请刷新或重试删除。'));
      else {
        const img = node('img'); img.src = signed.signedUrl; img.alt = photo.name; img.loading = 'lazy'; img.width = PHOTO_WIDTH; img.height = PHOTO_HEIGHT;
        img.addEventListener('error', () => { if (active()) say('预览地址可能已过期，请点击“刷新照片”。'); }); card.prepend(img);
      }
      const remove = button('删除照片', () => {
        if (busy || !active() || !confirm(`删除“${photo.name}”？所有厨房成员将无法再查看这张照片。`)) return;
        void operation(async () => {
          say('正在删除照片…');
          const { error: objectError } = await client.storage.from(BUCKET).remove([photo.object_path]);
          if (objectError) throw new Error('文件删除失败，照片记录已保留，请重试。');
          if (!active()) return;
          const { data: deleted, error: rowError } = await client.from('photos').delete().eq('id', photo.id).eq('household_id', householdId).select('id');
          if (rowError) throw new Error('文件已删除，但照片记录未清理，请重试删除。');
          if (!deleted?.length) throw new Error('照片记录未删除，可能权限已变化或照片已被家人删除，请刷新确认。');
          say('照片已删除。'); await load();
        });
      }); remove.className = 'secondary'; card.append(remove); gallery.append(card);
    }
  }
  input.addEventListener('change', () => {
    const file = input.files?.[0]; if (!file || busy) return;
    const version = ++selectionVersion; clearPreview();
    void operation(async () => {
      validateSource(file); say('正在准备裁切预览…');
      let bitmap: ImageBitmap;
      try { bitmap = await createImageBitmap(file, { imageOrientation: 'from-image' }); }
      catch { throw new Error('无法读取照片，请换一张 JPEG、PNG 或 WebP。'); }
      try {
        if (!active() || version !== selectionVersion) return;
        if (bitmap.width * bitmap.height > 48_000_000 || bitmap.width > 16000 || bitmap.height > 16000) throw new Error('照片尺寸过大，请先缩小到 4800 万像素以内。');
        const crop = coverCrop(bitmap.width, bitmap.height);
        const canvas = node('canvas'); canvas.width = PHOTO_WIDTH; canvas.height = PHOTO_HEIGHT;
        const context = canvas.getContext('2d'); if (!context) throw new Error('浏览器无法处理照片，请更换浏览器。');
        context.fillStyle = '#ffffff'; context.fillRect(0, 0, PHOTO_WIDTH, PHOTO_HEIGHT);
        context.drawImage(bitmap, crop.sx, crop.sy, crop.sw, crop.sh, 0, 0, PHOTO_WIDTH, PHOTO_HEIGHT);
        let blob: Blob | null = null;
        for (const quality of [0.86, 0.74, 0.6, 0.45]) {
          blob = await new Promise<Blob | null>(resolve => canvas.toBlob(resolve, 'image/jpeg', quality));
          if (blob && blob.size <= MAX_JPEG_BYTES) break;
        }
        if (!blob || blob.type !== 'image/jpeg' || blob.size > MAX_JPEG_BYTES) throw new Error('照片压缩失败或仍超过 1MB，请换一张照片。');
        if (!active() || version !== selectionVersion) return;
        selected = { blob, name: file.name.slice(0, 120) || '照片' }; previewUrl = URL.createObjectURL(blob);
        const img = node('img'); img.src = previewUrl; img.alt = '上传前裁切预览'; img.width = PHOTO_WIDTH; img.height = PHOTO_HEIGHT;
        const confirmUpload = button('确认上传', () => void operation(async () => {
          const upload = selected; if (!upload) return;
          say('正在上传照片…');
          const id = crypto.randomUUID(); const path = photoPath(householdId, id);
          const { error: uploadError } = await client.storage.from(BUCKET).upload(path, upload.blob, { contentType: 'image/jpeg', upsert: false });
          if (uploadError) throw new Error('上传失败，请检查网络或照片存储权限后重试。');
          // If session changed, never write metadata with a different account.
          if (!active()) return;
          const { error: rowError } = await client.from('photos').insert({ id, household_id: householdId, object_path: path, name: upload.name, created_by: userId });
          if (rowError) {
            const { error: cleanupError } = await client.storage.from(BUCKET).remove([path]);
            if (cleanupError) {
              const retry = button('重试清理未登记照片', () => void operation(async () => {
                const { error } = await client.storage.from(BUCKET).remove([path]);
                if (error) throw new Error('清理仍失败，请稍后重试。');
                retry.remove(); say('未登记照片已清理，可以重新上传。');
              })); preview.append(retry);
              throw new Error('照片记录保存失败，文件清理也未成功；请先点击重试清理。');
            }
            throw new Error('照片记录保存失败，已清理本次上传文件，请重试。');
          }
          if (!active()) return;
          clearPreview(); say('照片已上传。屏幕同步尚未接入。'); await load();
        }));
        const cancel = button('取消', () => { if (!busy) { ++selectionVersion; clearPreview(); say(); } }); cancel.className = 'secondary';
        preview.append(node('p', '画面将居中裁切为横屏比例，请确认主体完整。'), img, confirmUpload, cancel); say('预览已就绪，确认后才会上传。');
      } finally { bitmap.close(); }
    });
  });
  void load().catch(() => say('读取照片失败，请重试。'));
  return () => { disposed = true; ++selectionVersion; ++loadVersion; clearPreview(); gallery.replaceChildren(); };
}
