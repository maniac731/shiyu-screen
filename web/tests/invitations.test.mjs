import test from 'node:test';
import assert from 'node:assert/strict';
import { parseInvite, buildInviteUrl, invitationState } from '../src/invitations.mjs';
const token = 'ab'.repeat(32);
test('only accepts exact high entropy invitation format', () => {
  assert.equal(parseInvite(`#invite=${token.toUpperCase()}`), token);
  assert.equal(parseInvite('#invite=hello'), null);
  assert.equal(parseInvite(`#invite=${token}0`), null);
  assert.equal(parseInvite('#other=value'), null);
});
test('share URL keeps deployment path but drops query and old fragment', () => {
  assert.equal(buildInviteUrl('https://example.com/kitchen/?old=1#old', token), `https://example.com/kitchen/#invite=${token}`);
  assert.throws(() => buildInviteUrl('https://example.com', 'invalid'));
});
test('existing membership and expiry prevent accepting', () => {
  const preview = { expires_at: '2030-01-01T00:00:00Z' };
  assert.equal(invitationState(true, preview, 0), 'already-member');
  assert.equal(invitationState(false, preview, 0), 'available');
  assert.equal(invitationState(false, preview, Date.parse(preview.expires_at)), 'expired');
  assert.equal(invitationState(false, { expires_at: 'bad' }, 0), 'expired');
  assert.equal(invitationState(false, null), 'pending');
});
