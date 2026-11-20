-- Integration test for a disposable Supabase database, run as postgres with ON_ERROR_STOP.
-- Never run against production: uses fixed synthetic Auth users, then rolls back.
begin;
insert into auth.users(id) values
 ('11111111-1111-4111-8111-111111111111'),
 ('22222222-2222-4222-8222-222222222222'),
 ('33333333-3333-4333-8333-333333333333'),
 ('44444444-4444-4444-8444-444444444444');
create function pg_temp.assert_true(p_ok boolean, p_message text) returns void
language plpgsql as $$ begin
 if p_ok is distinct from true then raise exception 'assertion failed: %', p_message; end if;
end $$;
grant execute on function pg_temp.assert_true(boolean,text) to authenticated, anon;
select set_config('request.jwt.claim.sub', '11111111-1111-4111-8111-111111111111', true);
set local role authenticated;
select set_config('test.kitchen_a', public.create_household('Kitchen A')::text, true);
select set_config('test.invite', public.create_invitation(), true);
do $$ begin
  perform public.create_household('duplicate');
  raise exception 'duplicate membership accepted';
exception when unique_violation then null; end $$;
do $$ begin
  insert into public.household_members values ('44444444-4444-4444-8444-444444444444', current_setting('test.kitchen_a')::uuid, now());
  raise exception 'direct member insert accepted';
exception when insufficient_privilege then null; end $$;
do $$ begin
  update public.households set owner_id = '44444444-4444-4444-8444-444444444444';
  raise exception 'direct owner update accepted';
exception when insufficient_privilege then null; end $$;
do $$ begin
  delete from public.household_members;
  raise exception 'direct member delete accepted';
exception when insufficient_privilege then null; end $$;
do $$ begin
  perform 1 from shiyu_private.household_invitations;
  raise exception 'private invite read accepted';
exception when insufficient_privilege then null; end $$;
reset role;
select set_config('request.jwt.claim.sub', '22222222-2222-4222-8222-222222222222', true);
set local role authenticated;
select set_config('test.kitchen_b', public.create_household('Kitchen B')::text, true);
select pg_temp.assert_true((select count(*) = 1 from public.households), 'cross-household read isolation');
select pg_temp.assert_true((select count(*) = 1 from public.household_members), 'membership read isolation');
do $$ begin
  perform public.redeem_invitation(current_setting('test.invite'));
  raise exception 'second kitchen accepted';
exception when invalid_parameter_value then null; end $$;
reset role;
select set_config('request.jwt.claim.sub', '33333333-3333-4333-8333-333333333333', true);
set local role authenticated;
select pg_temp.assert_true((select household_name = 'Kitchen A' from public.preview_invitation(current_setting('test.invite'))), 'preview kitchen name');
select pg_temp.assert_true((select count(*) = 0 from public.household_members), 'preview grants no membership');
select pg_temp.assert_true((select household_name = 'Kitchen A' from public.preview_invitation(current_setting('test.invite'))), 'preview does not consume invite');
select pg_temp.assert_true(public.redeem_invitation(current_setting('test.invite')) = current_setting('test.kitchen_a')::uuid, 'join kitchen A');
select pg_temp.assert_true((select count(*) = 2 from public.household_members), 'member sees household membership');
do $$ begin
  perform public.create_invitation();
  raise exception 'member generated invite';
exception when insufficient_privilege then null; end $$;
do $$ begin
  perform public.remove_member('11111111-1111-4111-8111-111111111111');
  raise exception 'member removed owner';
exception when insufficient_privilege then null; end $$;
reset role;
select set_config('request.jwt.claim.sub', '44444444-4444-4444-8444-444444444444', true);
set local role authenticated;
do $$ begin
  perform public.redeem_invitation(current_setting('test.invite'));
  raise exception 'invite reused';
exception when invalid_parameter_value then null; end $$;
do $$ begin
  perform public.redeem_invitation(repeat('f',64));
  raise exception 'invalid invite accepted';
exception when invalid_parameter_value then null; end $$;
reset role;
select set_config('request.jwt.claim.sub', '11111111-1111-4111-8111-111111111111', true);
set local role authenticated;
do $$ begin
  perform public.remove_member('11111111-1111-4111-8111-111111111111');
  raise exception 'owner removed';
exception when invalid_parameter_value then null; end $$;
select public.remove_member('33333333-3333-4333-8333-333333333333');
select pg_temp.assert_true((select count(*) = 1 from public.household_members), 'owner removes member');
select set_config('test.expired', public.create_invitation(), true);
reset role;
update shiyu_private.household_invitations set expires_at = now() - interval '1 second'
 where code_hash = sha256(convert_to(current_setting('test.expired'), 'UTF8'));
select set_config('request.jwt.claim.sub', '44444444-4444-4444-8444-444444444444', true);
set local role authenticated;
do $$ begin
  perform public.redeem_invitation(current_setting('test.expired'));
  raise exception 'expired invite accepted';
exception when invalid_parameter_value then null; end $$;
reset role;
do $$ begin
  delete from auth.users where id = '11111111-1111-4111-8111-111111111111';
  raise exception 'owner auth deletion accepted';
exception when foreign_key_violation then null; end $$;
set local role anon;
do $$ begin
  perform public.preview_invitation(current_setting('test.invite'));
  raise exception 'anonymous preview accepted';
exception when insufficient_privilege then null; end $$;
do $$ begin
  perform public.create_household('anonymous');
  raise exception 'anonymous RPC accepted';
exception when insufficient_privilege then null; end $$;
do $$ begin
  perform 1 from public.households;
  raise exception 'anonymous table read accepted';
exception when insufficient_privilege then null; end $$;
reset role;
rollback;
