export const TOKEN_PATTERN = /^[0-9a-f]{64}$/i;
export const INVITE_STORAGE_KEY = 'shiyu.pending-invitation';
export function parseInvite(hash) {
  const value = new URLSearchParams(hash.replace(/^#/, '')).get('invite');
  return value && TOKEN_PATTERN.test(value) ? value.toLowerCase() : null;
}
export function buildInviteUrl(base, token) {
  if (!TOKEN_PATTERN.test(token)) throw new Error('Invalid invitation token');
  const url = new URL(base);
  url.search = '';
  url.hash = new URLSearchParams({ invite: token.toLowerCase() }).toString();
  return url.toString();
}
export function invitationState(hasKitchen, preview, now = Date.now()) {
  if (hasKitchen) return 'already-member';
  if (!preview) return 'pending';
  const expires = Date.parse(preview.expires_at);
  return Number.isFinite(expires) && expires > now ? 'available' : 'expired';
}
