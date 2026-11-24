begin;
insert into auth.users(id) values('b1111111-1111-4111-8111-111111111111'),('b2222222-2222-4222-8222-222222222222'),('b3333333-3333-4333-8333-333333333333');
create function pg_temp.stock_assert(ok boolean,msg text) returns void language plpgsql as $$ begin if ok is distinct from true then raise exception 'assertion failed: %',msg; end if; end $$;
grant execute on function pg_temp.stock_assert(boolean,text) to authenticated,service_role;
select set_config('request.jwt.claim.sub','b1111111-1111-4111-8111-111111111111',true);
set local role authenticated;
select set_config('test.stock_home',public.create_household('Stock A')::text,true);
select set_config('test.stock_item',public.inventory_write('add','牛奶',2,'盒',p_request=>'b1111111-1111-4111-8111-111111111110')::text,true);

select public.inventory_write('add','牛奶',1,'盒',p_request=>gen_random_uuid(),p_expires_on=>'2026-12-01');
select public.inventory_write('add','牛奶',2,'盒',p_request=>gen_random_uuid(),p_expires_on=>'2026-12-01');
select public.inventory_write('add','牛奶',1,'盒',p_request=>gen_random_uuid(),p_expires_on=>'2020-01-01');
select pg_temp.stock_assert((select count(*)=3 from public.inventory_items),'three separate batches');
select pg_temp.stock_assert((select quantity=3 from public.inventory_items where expires_on='2026-12-01'),'same expiry merges');
select public.inventory_write('update','牛奶',4,'盒',(select id from public.inventory_items where expires_on='2026-12-01'),2);
select pg_temp.stock_assert((select quantity=4 from public.inventory_items where expires_on='2026-12-01'),'legacy update preserves expiry');
select public.inventory_write('update','牛奶',4,'盒',(select id from public.inventory_items where expires_on='2026-12-01'),3,p_expires_on=>'2026-12-02');
do $$ begin
 perform public.inventory_write('update','牛奶',5,'盒',(select id from public.inventory_items where expires_on='2026-12-02'),4,p_expires_on=>null);
 raise exception 'colliding batch edit accepted';
exception when unique_violation then null; end $$;
do $$ begin
 perform public.inventory_write('add','牛奶',1,'瓶',p_request=>gen_random_uuid(),p_expires_on=>'2027-01-01');
 raise exception 'different date bypasses unit rule';
exception when sqlstate 'P1002' then null; end $$;
do $$ begin
 perform public.inventory_write('add','牛奶',1,'盒',p_request=>gen_random_uuid(),p_expires_on=>'2026-02-30');
 raise exception 'invalid calendar date accepted';
exception when invalid_parameter_value then null; end $$;
select public.inventory_write('add','面粉',1,'袋',p_request=>'b1111111-1111-4111-8111-111111111199',p_expires_on=>'2027-01-01');
do $$ begin
 perform public.inventory_write('add','面粉',1,'袋',p_request=>'b1111111-1111-4111-8111-111111111199',p_expires_on=>'2027-01-02');
 raise exception 'changed expiry retry accepted';
exception when invalid_parameter_value then null; end $$;
select public.inventory_write('update','面粉',1,'袋',(select id from public.inventory_items where name='面粉'),1,p_expires_on=>null);
select pg_temp.stock_assert((select expires_on is null from public.inventory_items where name='面粉'),'explicit null clears expiry');
reset role;
insert into shiyu_private.devices(id,secret_hash,household_id) values('b4444444-4444-4444-8444-444444444444',repeat('c',64),current_setting('test.stock_home')::uuid);
set local role service_role;
select pg_temp.stock_assert(public.device_inventory('b4444444-4444-4444-8444-444444444444',repeat('c',64))->'items'->0->>'expires_on'='2020-01-01','past batch first');
select pg_temp.stock_assert(public.device_inventory('b4444444-4444-4444-8444-444444444444',repeat('c',64))->'items'->1->>'expires_on'='2026-12-02','future date follows');
select pg_temp.stock_assert(public.device_inventory('b4444444-4444-4444-8444-444444444444',repeat('c',64))->'items'->2->'expires_on'='null'::jsonb,'undated last');
select set_config('test.expiry_revision',public.device_inventory('b4444444-4444-4444-8444-444444444444',repeat('c',64))->>'revision',true);
reset role;
set local role authenticated;
select public.inventory_write('update','牛奶',1,'盒',(select id from public.inventory_items where expires_on='2020-01-01'),1,p_expires_on=>'2020-01-02');
reset role;
set local role service_role;
select pg_temp.stock_assert(public.device_inventory('b4444444-4444-4444-8444-444444444444',repeat('c',64),current_setting('test.expiry_revision'))->>'state'='inventory','date-only edit changes revision');
reset role;
rollback;
