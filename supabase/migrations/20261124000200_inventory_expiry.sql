begin;
alter table public.inventory_items add column expires_on date;
alter table public.inventory_items drop constraint inventory_items_household_id_name_key_key;
create unique index inventory_batch_key on public.inventory_items(household_id,name_key,expires_on) nulls not distinct;
drop function public.inventory_write(text,text,numeric,text,uuid,bigint,uuid);
create function public.inventory_write(p_action text,p_name text default null,
 p_quantity numeric default null,p_unit text default null,p_item uuid default null,
 p_version bigint default null,p_request uuid default null,p_expires_on text default '') returns uuid
language plpgsql security definer set search_path='' as $$
declare u uuid:=auth.uid(); h uuid; item public.inventory_items%rowtype;
 previous shiyu_private.inventory_requests%rowtype; fingerprint bytea; expiry date;
begin
 if u is null then raise exception 'authentication required' using errcode='42501'; end if;
 select household_id into h from public.household_members where user_id=u for key share;
 if h is null then raise exception 'membership required' using errcode='42501'; end if;
 perform pg_advisory_xact_lock(hashtextextended('inventory:'||h::text,0));
 if p_action not in ('add','update','delete') or p_action is null then
  raise exception 'invalid action' using errcode='22023'; end if;
 if p_action <> 'delete' then
  p_name:=btrim(p_name); p_unit:=btrim(p_unit);
  if p_name is null or length(p_name) not between 1 and 40 or octet_length(p_name)>120 or p_name ~ '[[:cntrl:]]'
   or p_unit is null or length(p_unit) not between 1 and 8 or octet_length(p_unit)>24 or p_unit ~ '[[:cntrl:]]'
   or p_quantity is null or p_quantity<0 or p_quantity>999999.999
   or p_quantity<>round(p_quantity,3) or p_quantity::text in ('NaN','Infinity','-Infinity') then
    raise exception 'invalid inventory fields' using errcode='22023'; end if;
 end if;
 if p_expires_on is not null and p_expires_on<>'' then
  if p_expires_on !~ '^[0-9]{4}-[0-9]{2}-[0-9]{2}$' then raise exception 'invalid expiry' using errcode='22023'; end if;
  begin expiry:=p_expires_on::date; exception when datetime_field_overflow or invalid_datetime_format then
   raise exception 'invalid expiry' using errcode='22023'; end;
  if not isfinite(expiry) or expiry::text<>p_expires_on then raise exception 'invalid expiry' using errcode='22023'; end if;
 end if;
 if p_action='add' then
  if p_request is null or p_quantity<=0 then raise exception 'positive quantity and request required' using errcode='22023'; end if;
  fingerprint:=sha256(convert_to((case when expiry is null then jsonb_build_array(lower(p_name),p_quantity::numeric(12,3),p_unit) else jsonb_build_array(lower(p_name),p_quantity::numeric(12,3),p_unit,expiry) end)::text,'UTF8'));
  select * into previous from shiyu_private.inventory_requests where user_id=u and request_id=p_request;
  if found then
   if previous.household_id<>h or previous.fingerprint<>fingerprint then
    raise exception 'request payload changed' using errcode='22023'; end if;
   return previous.item_id;
  end if;
  if exists(select 1 from public.inventory_items where household_id=h and name_key=lower(p_name) and unit<>p_unit) then raise exception 'unit mismatch' using errcode='P1002'; end if;
  select * into item from public.inventory_items where household_id=h and name_key=lower(p_name) and expires_on is not distinct from expiry for update;
  if found then
   if item.unit<>p_unit then raise exception 'unit mismatch' using errcode='P1002'; end if;
   if item.quantity+p_quantity>999999.999 then raise exception 'quantity limit' using errcode='22023'; end if;
   update public.inventory_items set quantity=quantity+p_quantity,version=version+1,updated_at=clock_timestamp() where id=item.id;
  else
   if (select count(*) from public.inventory_items where household_id=h)>=100 then
    raise exception 'inventory limit reached' using errcode='P1003'; end if;
   insert into public.inventory_items(household_id,name,quantity,unit,expires_on) values(h,p_name,p_quantity,p_unit,expiry) returning * into item;
  end if;
  insert into shiyu_private.inventory_requests values(u,p_request,h,fingerprint,item.id);
  return item.id;
 end if;
 select * into item from public.inventory_items where id=p_item and household_id=h for update;
 if not found or p_version is null or item.version<>p_version then
  raise exception 'inventory changed; refresh' using errcode='P1001'; end if;
 if p_action='delete' then delete from public.inventory_items where id=item.id;
 else
  if exists(select 1 from public.inventory_items where household_id=h and name_key=lower(p_name) and id<>item.id and unit<>p_unit) then raise exception 'unit mismatch' using errcode='P1002'; end if;
  update public.inventory_items set name=p_name,quantity=p_quantity,unit=p_unit,
  expires_on=case when p_expires_on='' then item.expires_on else expiry end,
  version=version+1,updated_at=clock_timestamp() where id=item.id; end if;
 return item.id;
end $$;
revoke all on function public.inventory_write(text,text,numeric,text,uuid,bigint,uuid,text) from public,anon,authenticated;
grant execute on function public.inventory_write(text,text,numeric,text,uuid,bigint,uuid,text) to authenticated;

create or replace function public.device_inventory(p_device uuid,p_hash text,p_known text default '') returns jsonb
language plpgsql security definer set search_path='' as $$
declare d shiyu_private.devices%rowtype; items jsonb; revision text;
begin
 select * into d from shiyu_private.devices where id=p_device for share;
 if not found or d.secret_hash is distinct from p_hash or d.revoked_at is not null or d.household_id is null then
  raise exception 'device unavailable' using errcode='42501'; end if;
 select coalesce(jsonb_agg(jsonb_build_object('id',id,'name',name,'quantity',quantity::text,'unit',unit,'expires_on',expires_on)
  order by expires_on asc nulls last,name_key,id),'[]'::jsonb) into items from public.inventory_items where household_id=d.household_id;
 if jsonb_array_length(items)>100 then raise exception 'inventory limit' using errcode='54000'; end if;
 revision:=encode(sha256(convert_to(d.household_id::text||items::text,'UTF8')),'hex');
 if revision=p_known then return jsonb_build_object('state','unchanged','household_id',d.household_id,'revision',revision); end if;
 return jsonb_build_object('state','inventory','household_id',d.household_id,'revision',revision,'items',items);
end $$;
revoke all on function public.device_inventory(uuid,text,text) from public,anon,authenticated;
grant execute on function public.device_inventory(uuid,text,text) to service_role;
commit;
