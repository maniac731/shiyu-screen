import test from 'node:test';
import assert from 'node:assert/strict';
import { coverCrop, photoPath, validateSource, MAX_JPEG_BYTES } from '../src/photo-rules.mjs';
test('center cover crop preserves target aspect for portrait and landscape', () => {
  for (const [w, h] of [[4000, 3000], [3000, 4000], [6000, 2000], [1024, 600]]) {
    const c = coverCrop(w, h);
    assert.ok(c.sx >= 0 && c.sy >= 0 && c.sw <= w && c.sh <= h);
    assert.ok(Math.abs(c.sw / c.sh - 1024 / 600) < 1e-9);
    assert.equal(c.sx * 2 + c.sw, w); assert.equal(c.sy * 2 + c.sh, h);
  }
  assert.throws(() => coverCrop(0, 600));
});
test('source limits reject unsupported and oversized inputs', () => {
  validateSource({ type: 'image/jpeg', size: 20 * 1024 * 1024 });
  for (const file of [{ type: 'image/heic', size: 100 }, { type: 'image/png', size: 0 }, { type: 'image/webp', size: 20 * 1024 * 1024 + 1 }]) assert.throws(() => validateSource(file));
  assert.equal(MAX_JPEG_BYTES, 1048576);
});
test('object paths are bound to household and photo ids', () => {
  const h = '11111111-1111-4111-8111-111111111111', id = '22222222-2222-4222-8222-222222222222';
  assert.equal(photoPath(h, id), `${h}/${id}.jpg`);
  assert.throws(() => photoPath('../other', id)); assert.throws(() => photoPath(h, 'x.jpg'));
});
