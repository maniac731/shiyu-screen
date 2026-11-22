begin;
create table shiyu_private.devices (
 id uuid primary key, secret_hash text not null check(secret_hash ~ '^[0-9a-f]{64}$'),
 household_id uuid references public.households(id) on delete restrict,
 created_at timestamptz not null default now(), bound_at timestamptz, revoked_at timestamptz
);
create table shiyu_private.device_pairings (
 device_id uuid primary key references shiyu_private.devices(id),
 claim_hash text not null unique check(claim_hash ~ '^[0-9a-f]{64}$'),
 expires_at timestamptz not null, consumed_at timestamptz
);
alter table shiyu_private.devices enable row level security;
alter table shiyu_private.device_pairings enable row level security;
revoke all on shiyu_private.devices, shiyu_private.device_pairings from public, anon, authenticated;

-- Only trusted Edge Functions call this RPC. p_user comes from verified Auth.getUser.
create function public.device_control(p_action text, p_device uuid default null,
 p_hash text default null, p_claim text default null, p_user uuid default null)
returns jsonb language plpgsql security definer set search_path = '' as $$
declare d shiyu_private.devices%rowtype; q shiyu_private.device_pairings%rowtype;
 h public.households%rowtype; photo public.photos%rowtype; result jsonb;
begin
 if p_action in ('preview','claim','revoke','list') then
  select * into h from public.households where owner_id=p_user;
  if not found then raise exception 'owner required' using errcode='42501'; end if;
  if p_action='list' then
   select coalesce(jsonb_agg(jsonb_build_object('id',id,'bound_at',bound_at,'revoked_at',revoked_at)), '[]'::jsonb)
    into result from shiyu_private.devices where household_id=h.id;
   return jsonb_build_object('devices',result);
  elsif p_action='revoke' then
   select * into d from shiyu_private.devices where id=p_device for update;
   if not found or d.household_id is distinct from h.id then raise exception 'device unavailable' using errcode='42501'; end if;
   update shiyu_private.devices set revoked_at=coalesce(revoked_at,clock_timestamp()) where id=d.id;
   return '{"revoked":true}'::jsonb;
  end if;
  -- Lock device first, same order as pairing refresh, to avoid refresh/claim races.
  select * into d from shiyu_private.devices where id=(select device_id from shiyu_private.device_pairings where claim_hash=p_claim) for update;
  select * into q from shiyu_private.device_pairings where device_id=d.id for update;
  if d.id is null or d.revoked_at is not null or d.household_id is not null or q.claim_hash is distinct from p_claim
    or q.consumed_at is not null or q.expires_at<=clock_timestamp() then
   raise exception 'pairing unavailable' using errcode='22023';
  end if;
  if p_action='preview' then return jsonb_build_object('device_id',d.id,'expires_at',q.expires_at,'household_name',h.name); end if;
  update shiyu_private.devices set household_id=h.id,bound_at=clock_timestamp() where id=d.id;
  update shiyu_private.device_pairings set consumed_at=clock_timestamp() where device_id=d.id;
  return jsonb_build_object('device_id',d.id,'household_id',h.id);
 end if;
 if p_action='enroll' then
  if p_device is null or p_hash is null or p_hash !~ '^[0-9a-f]{64}$' then raise exception 'invalid credentials'; end if;
  perform pg_advisory_xact_lock(hashtextextended('shiyu-device-enroll',0));
  if not exists(select 1 from shiyu_private.devices where id=p_device) then
   -- Development bootstrap cap. Not manufacturing authentication; see deployment gate.
   if (select count(*) from shiyu_private.devices where created_at>now()-interval '1 day')>=100 then
    raise exception 'enrollment limit reached' using errcode='54000';
   end if;
   insert into shiyu_private.devices(id,secret_hash) values(p_device,p_hash);
  end if;
 end if;
 select * into d from shiyu_private.devices where id=p_device for update;
 if not found or d.secret_hash is distinct from p_hash or d.revoked_at is not null then
  raise exception 'invalid credentials' using errcode='42501';
 end if;
 if p_action='pairing' then
  if d.household_id is not null then raise exception 'already bound' using errcode='22023'; end if;
  if p_claim is null or p_claim !~ '^[0-9a-f]{64}$' then raise exception 'invalid pairing'; end if;
  select * into q from shiyu_private.device_pairings where device_id=d.id;
  if q.expires_at>clock_timestamp()+interval '9 minutes 55 seconds' then raise exception 'retry later' using errcode='54000'; end if;
  insert into shiyu_private.device_pairings(device_id,claim_hash,expires_at)
   values(d.id,p_claim,clock_timestamp()+interval '10 minutes')
   on conflict(device_id) do update set claim_hash=excluded.claim_hash,expires_at=excluded.expires_at,consumed_at=null;
  return jsonb_build_object('expires_at',(select expires_at from shiyu_private.device_pairings where device_id=d.id));
 elsif p_action in ('status','enroll') then
  return jsonb_build_object('state',case when d.household_id is null then 'unbound' else 'bound' end,
   'household_name',(select name from public.households where id=d.household_id));
 elsif p_action='manifest' then
  if d.household_id is null then return '{"state":"unbound"}'::jsonb; end if;
  select * into photo from public.photos where household_id=d.household_id order by created_at desc,id desc limit 1;
  if not found then return '{"state":"empty"}'::jsonb; end if;
  return jsonb_build_object('state','photo','photo',jsonb_build_object('id',photo.id,'object_path',photo.object_path));
 end if;
 raise exception 'invalid action' using errcode='22023';
end $$;
revoke all on function public.device_control(text,uuid,text,text,uuid) from public, anon, authenticated;
grant execute on function public.device_control(text,uuid,text,text,uuid) to service_role;
commit;
