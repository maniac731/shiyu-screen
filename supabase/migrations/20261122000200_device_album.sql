begin;
create function public.device_album(p_device uuid,p_hash text,p_photo uuid default null)
returns jsonb language plpgsql security definer set search_path='' as $$
declare d shiyu_private.devices%rowtype; result jsonb; total bigint;
begin
 select * into d from shiyu_private.devices where id=p_device;
 if not found or d.secret_hash is distinct from p_hash or d.revoked_at is not null or d.household_id is null then
  raise exception 'device unavailable' using errcode='42501';
 end if;
 if p_photo is not null then
  select jsonb_build_object('id',id,'object_path',object_path) into result from public.photos where id=p_photo and household_id=d.household_id;
  if result is null then raise exception 'photo unavailable' using errcode='42501'; end if;
  return result;
 end if;
 select count(*) into total from public.photos where household_id=d.household_id;
 select coalesce(jsonb_agg(jsonb_build_object('id',q.id,'bytes',case when o.metadata->>'size' ~ '^[0-9]{1,10}$' then (o.metadata->>'size')::bigint else null end) order by q.created_at desc,q.id desc),'[]'::jsonb) into result
 from (select id,object_path,created_at from public.photos where household_id=d.household_id order by created_at desc,id desc limit 20) q
 left join storage.objects o on o.bucket_id='kitchen-photos' and o.name=q.object_path;
 return jsonb_build_object('state','album','photos',result,'total',total,'truncated',total>20);
end $$;
revoke all on function public.device_album(uuid,text,uuid) from public,anon,authenticated;
grant execute on function public.device_album(uuid,text,uuid) to service_role;
commit;
