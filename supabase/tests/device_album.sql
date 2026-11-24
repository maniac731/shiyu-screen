begin;
insert into auth.users(id) values('a1111111-1111-4111-8111-111111111111'),('a2222222-2222-4222-8222-222222222222');
insert into public.households(id,name,owner_id) values('b1111111-1111-4111-8111-111111111111','A','a1111111-1111-4111-8111-111111111111'),('b2222222-2222-4222-8222-222222222222','B','a2222222-2222-4222-8222-222222222222');
insert into shiyu_private.devices(id,secret_hash,household_id) values('c1111111-1111-4111-8111-111111111111',repeat('a',64),'b1111111-1111-4111-8111-111111111111');
insert into public.photos(id,household_id,object_path,name,created_by)
select ('d1111111-1111-4111-8111-'||lpad(i::text,12,'0'))::uuid,'b1111111-1111-4111-8111-111111111111','b1111111-1111-4111-8111-111111111111/d1111111-1111-4111-8111-'||lpad(i::text,12,'0')||'.jpg','Photo','a1111111-1111-4111-8111-111111111111' from generate_series(1,25)i;
insert into public.photos values('d2222222-2222-4222-8222-222222222222','b2222222-2222-4222-8222-222222222222','b2222222-2222-4222-8222-222222222222/d2222222-2222-4222-8222-222222222222.jpg','Other',now(),'a2222222-2222-4222-8222-222222222222');
insert into storage.objects(bucket_id,name,metadata)select 'kitchen-photos',object_path,'{"size":1234}'::jsonb from public.photos;
set local role authenticated;
do $$begin perform public.device_album('c1111111-1111-4111-8111-111111111111',repeat('a',64));raise exception 'client access';exception when insufficient_privilege then null;end$$;
reset role;
set local role service_role;
do $$declare r jsonb;begin
 r:=public.device_album('c1111111-1111-4111-8111-111111111111',repeat('a',64));
 if jsonb_array_length(r->'photos')<>20 or (r->>'total')::int<>25 or not(r->>'truncated')::boolean or (r->'photos'->0->>'bytes')::int<>1234 or octet_length(r::text)>4096 then raise exception 'album bounds/metadata failed';end if;
 if r->'photos'->0->>'id'<>'d1111111-1111-4111-8111-000000000025' then raise exception 'order failed';end if;
 perform public.device_album('c1111111-1111-4111-8111-111111111111',repeat('a',64),'d1111111-1111-4111-8111-000000000025');
end$$;
do $$begin perform public.device_album('c1111111-1111-4111-8111-111111111111',repeat('b',64));raise exception 'wrong secret accepted';exception when insufficient_privilege then null;end$$;
do $$begin perform public.device_album('c1111111-1111-4111-8111-111111111111',repeat('a',64),'d2222222-2222-4222-8222-222222222222');raise exception 'cross kitchen accepted';exception when insufficient_privilege then null;end$$;
reset role;
update shiyu_private.devices set revoked_at=now();
set local role service_role;
do $$begin perform public.device_album('c1111111-1111-4111-8111-111111111111',repeat('a',64));raise exception 'revoked accepted';exception when insufficient_privilege then null;end$$;
reset role;
rollback;
