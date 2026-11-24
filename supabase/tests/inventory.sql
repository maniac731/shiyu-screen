begin;
insert into auth.users(id) values('b1111111-1111-4111-8111-111111111111'),('b2222222-2222-4222-8222-222222222222'),('b3333333-3333-4333-8333-333333333333');
create function pg_temp.stock_assert(ok boolean,msg text) returns void language plpgsql as $$ begin if ok is distinct from true then raise exception 'assertion failed: %',msg; end if; end $$;
grant execute on function pg_temp.stock_assert(boolean,text) to authenticated,service_role;
select set_config('request.jwt.claim.sub','b1111111-1111-4111-8111-111111111111',true);
set local role authenticated;
select set_config('test.stock_home',public.create_household('Stock A')::text,true);
select set_config('test.stock_item',public.inventory_write('add','牛奶',2,'盒',p_request=>'b1111111-1111-4111-8111-111111111110')::text,true);
select public.inventory_write('add','牛奶',2,'盒',p_request=>'b1111111-1111-4111-8111-111111111110');
select pg_temp.stock_assert((select quantity=2 from public.inventory_items),'retry does not double add');
do $$ begin
 perform public.inventory_write('add','牛奶',3,'盒',p_request=>'b1111111-1111-4111-8111-111111111110');
 raise exception 'changed nonce accepted';
exception when invalid_parameter_value then null; end $$;
do $$ begin
 perform public.inventory_write('add','牛奶',1,'瓶',p_request=>'b1111111-1111-4111-8111-111111111119');
 raise exception 'units mixed';
exception when sqlstate 'P1002' then null; end $$;
do $$ begin
 update public.inventory_items set quantity=99;
 raise exception 'direct writes allowed';
exception when insufficient_privilege then null; end $$;
reset role;
insert into public.household_members(user_id,household_id) values('b2222222-2222-4222-8222-222222222222',current_setting('test.stock_home')::uuid);
select set_config('request.jwt.claim.sub','b2222222-2222-4222-8222-222222222222',true);
set local role authenticated;
select public.inventory_write('add','牛奶',1,'盒',p_request=>'b2222222-2222-4222-8222-222222222220');
select pg_temp.stock_assert((select quantity=3 and version=2 from public.inventory_items),'member merges same name');
do $$ begin
 perform public.inventory_write('update','牛奶',7,'盒',current_setting('test.stock_item')::uuid,1);
 raise exception 'stale editor overwrote';
exception when sqlstate 'P1001' then null; end $$;
select public.inventory_write('update','牛奶',4.5,'盒',current_setting('test.stock_item')::uuid,2);
select pg_temp.stock_assert((select quantity=4.5 and version=3 from public.inventory_items),'edit changes total');
reset role;
select set_config('request.jwt.claim.sub','b3333333-3333-4333-8333-333333333333',true);
set local role authenticated;
select set_config('test.stock_other',public.create_household('Stock B')::text,true);
select pg_temp.stock_assert((select count(*)=0 from public.inventory_items),'cross kitchen RLS');
do $$ begin
 perform public.inventory_write('delete',p_item=>current_setting('test.stock_item')::uuid,p_version=>3);
 raise exception 'cross kitchen delete allowed';
exception when sqlstate 'P1001' then null; end $$;
reset role;
select pg_temp.stock_assert(shiyu_private.household_is_empty(current_setting('test.stock_other')::uuid,'b3333333-3333-4333-8333-333333333333'),'empty kitchen still switchable');
set local role authenticated;
select set_config('test.other_item',public.inventory_write('add','面粉',0.5,'千克',p_request=>'b3333333-3333-4333-8333-333333333330')::text,true);
reset role;
select pg_temp.stock_assert(not shiyu_private.household_is_empty(current_setting('test.stock_other')::uuid,'b3333333-3333-4333-8333-333333333333'),'inventory protects kitchen from deletion');
set local role authenticated;
select public.inventory_write('delete',p_item=>current_setting('test.other_item')::uuid,p_version=>1);
reset role;
select pg_temp.stock_assert(shiyu_private.household_is_empty(current_setting('test.stock_other')::uuid,'b3333333-3333-4333-8333-333333333333'),'deleting inventory restores empty predicate');
insert into shiyu_private.devices(id,secret_hash,household_id) values('b4444444-4444-4444-8444-444444444444',repeat('c',64),current_setting('test.stock_home')::uuid);
set local role service_role;
select pg_temp.stock_assert(jsonb_array_length(public.device_inventory('b4444444-4444-4444-8444-444444444444',repeat('c',64))->'items')=1,'device sees only bound stock');
select set_config('test.stock_revision',public.device_inventory('b4444444-4444-4444-8444-444444444444',repeat('c',64))->>'revision',true);
select pg_temp.stock_assert(public.device_inventory('b4444444-4444-4444-8444-444444444444',repeat('c',64),current_setting('test.stock_revision'))->>'state'='unchanged','unchanged response');
do $$ begin
 perform public.device_inventory('b4444444-4444-4444-8444-444444444444',repeat('d',64));
 raise exception 'wrong secret allowed';
exception when insufficient_privilege then null; end $$;
reset role;
update shiyu_private.devices set revoked_at=now() where id='b4444444-4444-4444-8444-444444444444';
set local role service_role;
do $$ begin
 perform public.device_inventory('b4444444-4444-4444-8444-444444444444',repeat('c',64));
 raise exception 'revoked device allowed';
exception when insufficient_privilege then null; end $$;
reset role;
select set_config('request.jwt.claim.sub','b3333333-3333-4333-8333-333333333333',true);
set local role authenticated;
do $$ begin
 perform public.inventory_write('add','精度',0.0001,'个',p_request=>gen_random_uuid());
 raise exception 'extra decimals accepted';
exception when invalid_parameter_value then null; end $$;
do $$ begin
 for i in 1..100 loop
  perform public.inventory_write('add','库存'||i,1,'个',p_request=>gen_random_uuid());
 end loop;
end $$;
do $$ begin
 perform public.inventory_write('add','超限',1,'个',p_request=>gen_random_uuid());
 raise exception '101st accepted';
exception when sqlstate 'P1003' then null; end $$;
reset role;
insert into shiyu_private.devices(id,secret_hash,household_id) values('b5555555-5555-4555-8555-555555555555',repeat('e',64),current_setting('test.stock_other')::uuid);
set local role service_role;
select pg_temp.stock_assert(jsonb_array_length(public.device_inventory('b5555555-5555-4555-8555-555555555555',repeat('e',64))->'items')=100,'full inventory not silently truncated');
select pg_temp.stock_assert(octet_length(public.device_inventory('b5555555-5555-4555-8555-555555555555',repeat('e',64))::text)<65536,'snapshot bound');
reset role;
set local role anon;
do $$ begin
 perform public.inventory_write('delete',p_item=>current_setting('test.stock_item')::uuid,p_version=>3);
 raise exception 'anonymous write allowed';
exception when insufficient_privilege then null; end $$;
reset role;
rollback;
