-- Disposable database only. Apply migrations 001..004 first; execute as postgres.
begin;
insert into auth.users(id) values
 ('a1111111-1111-4111-8111-111111111111'),('a2222222-2222-4222-8222-222222222222'),
 ('a3333333-3333-4333-8333-333333333333');
insert into public.households(id,name,owner_id) values
 ('b1111111-1111-4111-8111-111111111111','Test kitchen A','a1111111-1111-4111-8111-111111111111'),
 ('b2222222-2222-4222-8222-222222222222','Test kitchen B','a2222222-2222-4222-8222-222222222222');
insert into public.household_members(user_id,household_id) values
 ('a1111111-1111-4111-8111-111111111111','b1111111-1111-4111-8111-111111111111'),
 ('a2222222-2222-4222-8222-222222222222','b2222222-2222-4222-8222-222222222222'),
 ('a3333333-3333-4333-8333-333333333333','b1111111-1111-4111-8111-111111111111');
set local role authenticated;
do $$begin
 perform public.device_control('list',p_user=>'a1111111-1111-4111-8111-111111111111');
 raise exception 'client can forge service user';
exception when insufficient_privilege then null; end$$;
reset role;
set local role service_role;
select public.device_control('enroll','c1111111-1111-4111-8111-111111111111',repeat('a',64));
-- Retry of same enrollment must not replace credentials.
select public.device_control('enroll','c1111111-1111-4111-8111-111111111111',repeat('a',64));
do $$begin
 perform public.device_control('enroll','c1111111-1111-4111-8111-111111111111',repeat('b',64));
 raise exception 'credential replaced';
exception when insufficient_privilege then null; end$$;
select public.device_control('pairing','c1111111-1111-4111-8111-111111111111',repeat('a',64),repeat('c',64));
do $$begin
 perform public.device_control('claim',p_claim=>repeat('c',64),p_user=>'a3333333-3333-4333-8333-333333333333');
 raise exception 'member claimed device';
exception when insufficient_privilege then null; end$$;
select public.device_control('preview',p_claim=>repeat('c',64),p_user=>'a1111111-1111-4111-8111-111111111111');
select public.device_control('claim',p_claim=>repeat('c',64),p_user=>'a1111111-1111-4111-8111-111111111111');
do $$declare s jsonb;begin
 s:=public.device_control('status','c1111111-1111-4111-8111-111111111111',repeat('a',64));
 if s->>'state'<>'bound' then raise exception 'status not bound'; end if;
end$$;
do $$begin
 perform public.device_control('claim',p_claim=>repeat('c',64),p_user=>'a2222222-2222-4222-8222-222222222222');
 raise exception 'pairing replay accepted';
exception when invalid_parameter_value then null; end$$;
do $$begin
 perform public.device_control('revoke','c1111111-1111-4111-8111-111111111111',p_user=>'a2222222-2222-4222-8222-222222222222');
 raise exception 'cross kitchen revoke';
exception when insufficient_privilege then null; end$$;
select public.device_control('revoke','c1111111-1111-4111-8111-111111111111',p_user=>'a1111111-1111-4111-8111-111111111111');
do $$begin
 perform public.device_control('manifest','c1111111-1111-4111-8111-111111111111',repeat('a',64));
 raise exception 'revoked device read';
exception when insufficient_privilege then null; end$$;
reset role;
rollback;
