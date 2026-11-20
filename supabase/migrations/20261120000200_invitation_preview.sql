begin;
-- Preview does not consume the token. Redemption rechecks under a row lock.
create function public.preview_invitation(p_code text)
returns table (household_name text, expires_at timestamptz)
language plpgsql security definer set search_path = '' as $$
declare v_user uuid := auth.uid();
begin
  if v_user is null then
    raise exception 'authentication required' using errcode = '42501';
  end if;
  if exists (select 1 from public.household_members where user_id = v_user)
     or p_code is null or p_code !~ '^[0-9a-fA-F]{64}$' then
    raise exception 'invitation unavailable' using errcode = '22023';
  end if;
  return query select h.name, i.expires_at
    from shiyu_private.household_invitations i
    join public.households h on h.id = i.household_id
    where i.code_hash = pg_catalog.sha256(pg_catalog.convert_to(lower(p_code), 'UTF8'))
      and i.redeemed_at is null and i.expires_at > clock_timestamp();
  if not found then
    raise exception 'invitation unavailable' using errcode = '22023';
  end if;
end $$;
revoke all on function public.preview_invitation(text) from public, anon, authenticated;
grant execute on function public.preview_invitation(text) to authenticated;
commit;
