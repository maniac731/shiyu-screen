export const PHOTO_WIDTH = 1024;
export const PHOTO_HEIGHT = 600;
export const MAX_JPEG_BYTES = 1024 * 1024;
export function coverCrop(width, height) {
  if (!Number.isFinite(width) || !Number.isFinite(height) || width <= 0 || height <= 0) throw new Error('无效的照片尺寸');
  const ratio = PHOTO_WIDTH / PHOTO_HEIGHT;
  const sw = Math.min(width, height * ratio);
  const sh = sw / ratio;
  return { sx: (width - sw) / 2, sy: (height - sh) / 2, sw, sh };
}
export function photoPath(household, id) {
  const uuid = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;
  if (!uuid.test(household) || !uuid.test(id)) throw new Error('无效的照片归属');
  return `${household}/${id}.jpg`;
}
export function validateSource(file) {
  if (!['image/jpeg', 'image/png', 'image/webp'].includes(file.type)) throw new Error('请选择 JPEG、PNG 或 WebP；HEIC 请先转换为 JPEG。');
  if (file.size <= 0 || file.size > 20 * 1024 * 1024) throw new Error('原照片需小于或等于 20MB。');
}
