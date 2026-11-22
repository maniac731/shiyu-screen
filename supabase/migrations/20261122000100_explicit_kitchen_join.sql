begin;
-- Storage has no household FK: hold the membership row through each upload/delete.
-- After a concurrent switch deletes that row, a waiting operation fails closed.
create or replace function shiyu_private.photo_path_allowed(p_name text) returns boolean
language plpgsql volatile security definer set search_path = '' as $$
declare v_household uuid;
begin
  if auth.uid() is null or p_name is null or p_name !~ '^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}/[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\.jpg$' then return false; end if;
  select household_id into v_household from public.household_members
    where user_id = auth.uid() for key share;
  return coalesce(split_part(p_name, '/', 1) = v_household::text, false);
end $$;

create function shiyu_private.household_is_empty(p_household uuid, p_user uuid) returns boolean
language sql stable security definer set search_path = '' as $$
 select exists(select 1 from public.households where id=p_household and owner_id=p_user)
 and not exists(select 1 from public.household_members where household_id=p_household and user_id<>p_user)
 and not exists(select 1 from public.photos where household_id=p_household)
 and not exists(select 1 from storage.objects where bucket_id='kitchen-photos' and split_part(name,'/',1)=p_household::text)
 and not exists(select 1 from shiyu_private.devices where household_id=p_household)
 -- Inventory is not implemented. Fail closed if a future inventory table appears,
 -- until its content and concurrent-write checks are explicitly integrated here.
 and to_regclass('public.inventory_items') is null and to_regclass('public.inventory') is null
$$;
revoke all on function shiyu_private.household_is_empty(uuid,uuid) from public,anon,authenticated;

create function public.preview_household_join(p_code text)
returns table(household_name text, expires_at timestamptz, switch_required boolean, can_join boolean)
language plpgsql security definer set search_path = '' as $$
declare v_user uuid:=auth.uid(); v_old uuid;
begin
 if v_user is null then raise exception 'authentication required' using errcode='42501'; end if;
 if p_code is null or p_code !~ '^[0-9a-fA-F]{64}$' then raise exception 'invitation unavailable' using errcode='22023'; end if;
 select household_id into v_old from public.household_members where user_id=v_user;
 return query select h.name,i.expires_at,v_old is not null,
  v_old is null or (v_old<>h.id and shiyu_private.household_is_empty(v_old,v_user))
 from shiyu_private.household_invitations i join public.households h on h.id=i.household_id
 where i.code_hash=sha256(convert_to(lower(p_code),'UTF8')) and i.redeemed_at is null and i.expires_at>clock_timestamp();
 if not found then raise exception 'invitation unavailable' using errcode='22023'; end if;
end $$;

create function public.accept_household_join(p_code text, p_confirm_switch boolean default false) returns uuid
language plpgsql security definer set search_path = '' as $$
declare v_user uuid:=auth.uid(); v_old uuid; v_invite shiyu_private.household_invitations%rowtype;
begin
 if v_user is null then raise exception 'authentication required' using errcode='42501'; end if;
 perform pg_advisory_xact_lock(hashtextextended(v_user::text,0));
 if p_code is null or p_code !~ '^[0-9a-fA-F]{64}$' then raise exception 'invitation unavailable' using errcode='22023'; end if;
 select * into v_invite from shiyu_private.household_invitations
 where code_hash=sha256(convert_to(lower(p_code),'UTF8')) for update;
 if not found or v_invite.redeemed_at is not null or v_invite.expires_at<=clock_timestamp() then
  raise exception 'invitation unavailable' using errcode='22023'; end if;
 select household_id into v_old from public.household_members where user_id=v_user for update;
 if v_old is not null then
  if p_confirm_switch is distinct from true or v_old=v_invite.household_id then
   raise exception 'explicit switch confirmation required' using errcode='22023'; end if;
  -- FK inserts (members/photos/device claims) conflict with this lock. Storage
  -- writes conflict with the membership lock above. Recheck after acquiring both.
  perform 1 from public.households where id=v_old for update;
  if not shiyu_private.household_is_empty(v_old,v_user) then
   raise exception 'current kitchen is not empty' using errcode='P0001'; end if;
  delete from public.household_members where user_id=v_user;
  delete from public.households where id=v_old; -- cascades only old invitations
 end if;
 insert into public.household_members(user_id,household_id) values(v_user,v_invite.household_id);
 update shiyu_private.household_invitations set redeemed_by=v_user,redeemed_at=clock_timestamp() where id=v_invite.id;
 return v_invite.household_id;
end $$;
revoke all on function public.preview_household_join(text),public.accept_household_join(text,boolean) from public,anon,authenticated;
grant execute on function public.preview_household_join(text),public.accept_household_join(text,boolean) to authenticated;
commit;
