-- Disposable DB only. All synthetic fixtures roll back.
begin;
insert into auth.users(id) values
 ('a1111111-1111-4111-8111-111111111111'),('a2222222-2222-4222-8222-222222222222'),
 ('a3333333-3333-4333-8333-333333333333');
create function pg_temp.assert_join(ok boolean,msg text) returns void language plpgsql as $$
begin if ok is distinct from true then raise exception 'assertion failed: %',msg; end if; end $$;
grant execute on function pg_temp.assert_join(boolean,text) to authenticated;
select set_config('request.jwt.claim.sub','a1111111-1111-4111-8111-111111111111',true);
set local role authenticated;
select set_config('test.target',public.create_household('Family')::text,true);
select set_config('test.token',public.create_invitation(),true);
reset role;
select set_config('request.jwt.claim.sub','a2222222-2222-4222-8222-222222222222',true);
set local role authenticated;
select pg_temp.assert_join((select count(*)=0 from public.households),'registration does not create kitchen');
select set_config('test.old',public.create_household('Empty')::text,true);
select set_config('test.old_token',public.create_invitation(),true);
select pg_temp.assert_join((select switch_required and can_join from public.preview_household_join(current_setting('test.token'))),'empty owner can preview switch');
do $$ begin
 perform public.accept_household_join(current_setting('test.token'));
 raise exception 'switch accepted without confirmation';
exception when invalid_parameter_value then null; end $$;
select pg_temp.assert_join((select id=current_setting('test.old')::uuid from public.households),'unconfirmed preserves old kitchen');
-- An unregistered Storage upload alone must block the switch.
insert into storage.objects(bucket_id,name,metadata) values('kitchen-photos',current_setting('test.old')||'/bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb.jpg','{"mimetype":"image/jpeg","size":10}');
select pg_temp.assert_join((select not can_join from public.preview_household_join(current_setting('test.token'))),'orphan upload blocks switch');
do $$ begin
 perform public.accept_household_join(current_setting('test.token'),true);
 raise exception 'photo kitchen switched';
exception when raise_exception then
 if sqlerrm <> 'current kitchen is not empty' then raise; end if;
end $$;
select pg_temp.assert_join((select count(*)=1 from storage.objects),'blocked switch preserves uploaded object');
delete from storage.objects where bucket_id='kitchen-photos';
reset role;
insert into shiyu_private.devices(id,secret_hash,household_id,revoked_at) values
 ('bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb',repeat('b',64),current_setting('test.old')::uuid,now());
set local role authenticated;
select pg_temp.assert_join((select not can_join from public.preview_household_join(current_setting('test.token'))),'even revoked device blocks switch');
do $$ begin
 perform public.accept_household_join(current_setting('test.token'),true);
 raise exception 'device kitchen switched';
exception when raise_exception then if sqlerrm <> 'current kitchen is not empty' then raise; end if; end $$;
reset role;
delete from shiyu_private.devices where household_id=current_setting('test.old')::uuid;
insert into public.household_members(user_id,household_id) values('a3333333-3333-4333-8333-333333333333',current_setting('test.old')::uuid);
set local role authenticated;
select pg_temp.assert_join((select not can_join from public.preview_household_join(current_setting('test.token'))),'other member blocks switch');
reset role;
delete from public.household_members where user_id='a3333333-3333-4333-8333-333333333333';
set local role authenticated;
select pg_temp.assert_join(public.accept_household_join(current_setting('test.token'),true)=current_setting('test.target')::uuid,'confirmed empty switch');
select pg_temp.assert_join(not shiyu_private.photo_path_allowed(current_setting('test.old')||'/bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb.jpg'),'old upload access revoked');
reset role;
select pg_temp.assert_join(not exists(select 1 from public.households where id=current_setting('test.old')::uuid),'empty kitchen deleted');
select pg_temp.assert_join(not exists(select 1 from shiyu_private.household_invitations where code_hash=sha256(convert_to(current_setting('test.old_token'),'UTF8'))),'old invites removed');
select set_config('request.jwt.claim.sub','a3333333-3333-4333-8333-333333333333',true);
set local role authenticated;
do $$ begin
 perform public.accept_household_join(current_setting('test.token'));
 raise exception 'token reused';
exception when invalid_parameter_value then null; end $$;
reset role;
select set_config('request.jwt.claim.sub','a1111111-1111-4111-8111-111111111111',true);
set local role authenticated;
select set_config('test.new_token',public.create_invitation(),true);
reset role;
select set_config('request.jwt.claim.sub','a3333333-3333-4333-8333-333333333333',true);
set local role authenticated;
select pg_temp.assert_join((select not switch_required and can_join from public.preview_household_join(current_setting('test.new_token'))),'new member preview');
select pg_temp.assert_join(public.accept_household_join(current_setting('test.new_token'))=current_setting('test.target')::uuid,'account without kitchen joins directly');
select pg_temp.assert_join((select count(*)=3 from public.household_members),'three separate accounts share one kitchen');
reset role;
set local role anon;
do $$ begin
 perform public.accept_household_join(repeat('a',64),true);
 raise exception 'anonymous joined';
exception when insufficient_privilege then null; end $$;
reset role;
rollback;
