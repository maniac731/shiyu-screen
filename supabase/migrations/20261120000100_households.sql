-- Run as the migration owner (postgres). No remote project is changed by this file.
begin;
create schema if not exists shiyu_private;
revoke all on schema shiyu_private from public, anon, authenticated;

create table public.households (
  id uuid primary key default gen_random_uuid(),
  name text not null check (length(btrim(name)) between 1 and 80),
  owner_id uuid not null references auth.users(id) on delete restrict,
  created_at timestamptz not null default now()
);
create table public.household_members (
  user_id uuid primary key references auth.users(id) on delete restrict,
  household_id uuid not null references public.households(id) on delete restrict,
  joined_at timestamptz not null default now()
);
create index household_members_household_idx on public.household_members(household_id);
-- Owner role is derived from households.owner_id, never independently editable.
create table shiyu_private.household_invitations (
  id uuid primary key default gen_random_uuid(),
  household_id uuid not null references public.households(id) on delete cascade,
  code_hash bytea not null unique,
  created_by uuid not null references auth.users(id) on delete restrict,
  created_at timestamptz not null default now(),
  expires_at timestamptz not null default (now() + interval '24 hours'),
  redeemed_by uuid references auth.users(id) on delete restrict,
  redeemed_at timestamptz,
  check ((redeemed_by is null) = (redeemed_at is null))
);
create index household_invitations_household_idx on shiyu_private.household_invitations(household_id);
alter table public.households enable row level security;
alter table public.household_members enable row level security;
alter table shiyu_private.household_invitations enable row level security;
revoke all on public.households, public.household_members from public, anon, authenticated;
revoke all on shiyu_private.household_invitations from public, anon, authenticated;
grant select on public.households, public.household_members to authenticated;

create function shiyu_private.current_household_id() returns uuid
language sql stable security definer set search_path = '' as $$
  select household_id from public.household_members where user_id = (select auth.uid())
$$;
revoke all on function shiyu_private.current_household_id() from public, anon, authenticated;
grant usage on schema shiyu_private to authenticated;
grant execute on function shiyu_private.current_household_id() to authenticated;
create policy members_read_household on public.households for select to authenticated
using (id = (select shiyu_private.current_household_id()));
create policy members_read_members on public.household_members for select to authenticated
using (household_id = (select shiyu_private.current_household_id()));

create function public.create_household(p_name text) returns uuid
language plpgsql security definer set search_path = '' as $$
declare v_user uuid := auth.uid(); v_id uuid;
begin
  if v_user is null then raise exception 'authentication required' using errcode = '42501'; end if;
  perform pg_catalog.pg_advisory_xact_lock(pg_catalog.hashtextextended(v_user::text, 0));
  if exists (select 1 from public.household_members where user_id = v_user) then
    raise exception 'account already belongs to a kitchen' using errcode = '23505';
  end if;
  insert into public.households(name, owner_id) values (btrim(p_name), v_user) returning id into v_id;
  insert into public.household_members(user_id, household_id) values (v_user, v_id);
  return v_id;
end $$;

create function public.create_invitation() returns text
language plpgsql security definer set search_path = '' as $$
declare v_user uuid := auth.uid(); v_household uuid; v_code text;
begin
  if v_user is null then raise exception 'authentication required' using errcode = '42501'; end if;
  select h.id into v_household from public.households h
    join public.household_members m on m.household_id = h.id
    where m.user_id = v_user and h.owner_id = v_user for update of h;
  if v_household is null then raise exception 'owner required' using errcode = '42501'; end if;
  v_code := replace(pg_catalog.gen_random_uuid()::text || pg_catalog.gen_random_uuid()::text, '-', '');
  insert into shiyu_private.household_invitations(household_id, code_hash, created_by)
    values (v_household, pg_catalog.sha256(pg_catalog.convert_to(v_code, 'UTF8')), v_user);
  return v_code; -- Returned once; never persist plaintext in database or logs.
end $$;

create function public.redeem_invitation(p_code text) returns uuid
language plpgsql security definer set search_path = '' as $$
declare v_user uuid := auth.uid(); v_invite shiyu_private.household_invitations%rowtype;
begin
  if v_user is null then raise exception 'authentication required' using errcode = '42501'; end if;
  perform pg_catalog.pg_advisory_xact_lock(pg_catalog.hashtextextended(v_user::text, 0));
  if exists (select 1 from public.household_members where user_id = v_user) then
    raise exception 'invitation unavailable' using errcode = '22023';
  end if;
  if p_code is null or length(p_code) <> 64 then
    raise exception 'invitation unavailable' using errcode = '22023';
  end if;
  select * into v_invite from shiyu_private.household_invitations
    where code_hash = pg_catalog.sha256(pg_catalog.convert_to(lower(btrim(p_code)), 'UTF8')) for update;
  if not found or v_invite.redeemed_at is not null or v_invite.expires_at <= clock_timestamp() then
    raise exception 'invitation unavailable' using errcode = '22023';
  end if;
  insert into public.household_members(user_id, household_id) values (v_user, v_invite.household_id);
  update shiyu_private.household_invitations set redeemed_by = v_user, redeemed_at = clock_timestamp()
    where id = v_invite.id;
  return v_invite.household_id;
end $$;

create function public.remove_member(p_user_id uuid) returns void
language plpgsql security definer set search_path = '' as $$
declare v_user uuid := auth.uid(); v_household uuid;
begin
  if v_user is null then raise exception 'authentication required' using errcode = '42501'; end if;
  select id into v_household from public.households where owner_id = v_user for update;
  if v_household is null then raise exception 'owner required' using errcode = '42501'; end if;
  if p_user_id = v_user then raise exception 'owner cannot be removed' using errcode = '22023'; end if;
  delete from public.household_members where user_id = p_user_id and household_id = v_household;
end $$;

revoke all on function public.create_household(text), public.create_invitation(),
  public.redeem_invitation(text), public.remove_member(uuid) from public, anon, authenticated;
grant execute on function public.create_household(text), public.create_invitation(),
  public.redeem_invitation(text), public.remove_member(uuid) to authenticated;
commit;
