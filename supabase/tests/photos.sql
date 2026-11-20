-- Disposable Supabase DB only. Synthetic metadata is not an actual uploaded image.
-- Apply all migrations first, then run as postgres with ON_ERROR_STOP; always rollback.
begin;
insert into auth.users(id) values
 ('51111111-1111-4111-8111-111111111111'),
 ('52222222-2222-4222-8222-222222222222'),
 ('53333333-3333-4333-8333-333333333333');
create function pg_temp.assert_true(p_ok boolean, p_message text) returns void
language plpgsql as $$ begin
  if p_ok is distinct from true then raise exception 'assertion failed: %', p_message; end if;
end $$;
grant execute on function pg_temp.assert_true(boolean,text) to authenticated, anon;
select set_config('request.jwt.claim.sub', '51111111-1111-4111-8111-111111111111', true);
set local role authenticated;
select set_config('test.photo_a', public.create_household('Photos A')::text, true);
select set_config('test.photo_invite', public.create_invitation(), true);
reset role;
select set_config('request.jwt.claim.sub', '52222222-2222-4222-8222-222222222222', true);
set local role authenticated;
select set_config('test.photo_b', public.create_household('Photos B')::text, true);
reset role;
select set_config('request.jwt.claim.sub', '53333333-3333-4333-8333-333333333333', true);
set local role authenticated;
select public.redeem_invitation(current_setting('test.photo_invite'));
select set_config('test.photo_path', current_setting('test.photo_a') || '/54444444-4444-4444-8444-444444444444.jpg', true);
select pg_temp.assert_true(shiyu_private.photo_path_allowed(current_setting('test.photo_path')), 'member upload path');
select pg_temp.assert_true(not shiyu_private.photo_path_allowed(current_setting('test.photo_a') || '/nested/file.jpg'), 'reject nested path');
select pg_temp.assert_true(not shiyu_private.photo_path_allowed(current_setting('test.photo_b') || '/54444444-4444-4444-8444-444444444444.jpg'), 'reject other kitchen');
do $$ begin
  insert into public.photos(id,household_id,object_path,name) values
    ('54444444-4444-4444-8444-444444444444',current_setting('test.photo_a')::uuid,current_setting('test.photo_path'),'synthetic');
  raise exception 'missing Storage object accepted';
exception when insufficient_privilege then null; end $$;
-- Exercise INSERT policy; API MIME/size limits need separate Storage API tests.
insert into storage.objects(bucket_id,name,metadata) values
 ('kitchen-photos',current_setting('test.photo_path'),'{"mimetype":"image/jpeg","size":1000}'::jsonb);
do $$ begin
  insert into public.photos(id,household_id,object_path,name,created_by) values
   ('54444444-4444-4444-8444-444444444444',current_setting('test.photo_a')::uuid,current_setting('test.photo_path'),'spoof',
    '51111111-1111-4111-8111-111111111111');
  raise exception 'spoofed creator accepted';
exception when insufficient_privilege then null; end $$;
insert into public.photos(id,household_id,object_path,name) values
 ('54444444-4444-4444-8444-444444444444',current_setting('test.photo_a')::uuid,current_setting('test.photo_path'),'synthetic');
select pg_temp.assert_true((select count(*) = 1 from public.photos), 'member reads photo');
delete from public.photos;
select pg_temp.assert_true((select count(*) = 1 from public.photos), 'metadata cannot delete before file');
do $$ begin
  update storage.objects set name = name where bucket_id = 'kitchen-photos';
  if found then raise exception 'update unexpectedly allowed'; end if;
end $$;
reset role;
select set_config('request.jwt.claim.sub', '52222222-2222-4222-8222-222222222222', true);
set local role authenticated;
select pg_temp.assert_true((select count(*) = 0 from public.photos), 'cross kitchen photos invisible');
select pg_temp.assert_true((select count(*) = 0 from storage.objects where bucket_id = 'kitchen-photos'), 'cross kitchen objects invisible');
do $$ begin
  insert into storage.objects(bucket_id,name,metadata) values
   ('kitchen-photos',current_setting('test.photo_path'),'{"mimetype":"image/jpeg","size":1000}'::jsonb);
  raise exception 'cross kitchen object insert allowed';
exception when insufficient_privilege then null; end $$;
delete from public.photos;
delete from storage.objects where bucket_id = 'kitchen-photos';
reset role;
set local role anon;
do $$ begin
  perform 1 from public.photos;
  raise exception 'anonymous table read allowed';
exception when insufficient_privilege then null; end $$;
select pg_temp.assert_true((select count(*) = 0 from storage.objects where bucket_id = 'kitchen-photos'), 'anonymous objects invisible');
reset role;
select set_config('request.jwt.claim.sub', '51111111-1111-4111-8111-111111111111', true);
set local role authenticated;
select pg_temp.assert_true((select count(*) = 1 from public.photos), 'owner reads member photo');
-- In this rollback-only test delete synthetic metadata directly. Production MUST use Storage API.
delete from storage.objects where bucket_id = 'kitchen-photos' and name = current_setting('test.photo_path');
delete from public.photos where object_path = current_setting('test.photo_path');
select pg_temp.assert_true((select count(*) = 0 from public.photos), 'member/owner equal delete permission');
reset role;
rollback;
