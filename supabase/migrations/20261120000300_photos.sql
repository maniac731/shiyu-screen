begin;

-- Fail rather than silently changing an existing bucket owned by another feature.
do $$ begin
  if exists (select 1 from storage.buckets where id = 'kitchen-photos') then
    if not exists (select 1 from storage.buckets where id = 'kitchen-photos'
      and public = false and file_size_limit = 1048576
      and allowed_mime_types = array['image/jpeg']::text[]) then
      raise exception 'kitchen-photos bucket exists with incompatible configuration';
    end if;
  else
    insert into storage.buckets(id, name, public, file_size_limit, allowed_mime_types)
      values ('kitchen-photos', 'kitchen-photos', false, 1048576, array['image/jpeg']);
  end if;
end $$;

create table public.photos (
  id uuid primary key,
  household_id uuid not null references public.households(id) on delete restrict,
  object_path text not null unique,
  name text not null check (length(btrim(name)) between 1 and 160),
  created_at timestamptz not null default now(),
  created_by uuid not null default auth.uid() references auth.users(id) on delete restrict,
  check (object_path = household_id::text || '/' || id::text || '.jpg')
);
create index photos_household_created_idx on public.photos(household_id, created_at desc, id);
alter table public.photos enable row level security;
revoke all on public.photos from public, anon, authenticated;
grant select, insert, delete on public.photos to authenticated;

create function shiyu_private.photo_path_allowed(p_name text) returns boolean
language sql stable security definer set search_path = '' as $$
  select auth.uid() is not null
    and p_name ~ '^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}/[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\.jpg$'
    and split_part(p_name, '/', 1) = shiyu_private.current_household_id()::text
$$;

-- Definer only reads Storage metadata; never deletes it or exposes arbitrary paths.
create function shiyu_private.photo_object_exists(p_name text) returns boolean
language sql stable security definer set search_path = '' as $$
  select shiyu_private.photo_path_allowed(p_name) and exists (
    select 1 from storage.objects where bucket_id = 'kitchen-photos' and name = p_name
  )
$$;
create function shiyu_private.photo_upload_valid(p_name text) returns boolean
language sql stable security definer set search_path = '' as $$
  select shiyu_private.photo_path_allowed(p_name) and exists (
    select 1 from storage.objects where bucket_id = 'kitchen-photos' and name = p_name
      and metadata->>'mimetype' = 'image/jpeg'
      and case when metadata->>'size' ~ '^[0-9]{1,10}$'
        then (metadata->>'size')::bigint between 1 and 1048576 else false end
  )
$$;
revoke all on function shiyu_private.photo_path_allowed(text),
  shiyu_private.photo_object_exists(text), shiyu_private.photo_upload_valid(text)
  from public, anon, authenticated;
grant execute on function shiyu_private.photo_path_allowed(text),
  shiyu_private.photo_object_exists(text), shiyu_private.photo_upload_valid(text)
  to authenticated;

create policy photos_read on public.photos for select to authenticated
  using (household_id = (select shiyu_private.current_household_id()));
create policy photos_insert on public.photos for insert to authenticated
  with check (household_id = (select shiyu_private.current_household_id())
    and created_by = (select auth.uid()) and shiyu_private.photo_upload_valid(object_path));
-- Storage API remove must succeed first. A missing object is safe to retry.
create policy photos_delete on public.photos for delete to authenticated
  using (household_id = (select shiyu_private.current_household_id())
    and not shiyu_private.photo_object_exists(object_path));

create policy kitchen_photos_read on storage.objects for select to authenticated
  using (bucket_id = 'kitchen-photos' and shiyu_private.photo_path_allowed(name));
create policy kitchen_photos_insert on storage.objects for insert to authenticated
  with check (bucket_id = 'kitchen-photos' and shiyu_private.photo_path_allowed(name));
create policy kitchen_photos_delete on storage.objects for delete to authenticated
  using (bucket_id = 'kitchen-photos' and shiyu_private.photo_path_allowed(name));

-- Restrictive fences also protect this bucket if another feature has broad policies.
create policy kitchen_photos_member_fence on storage.objects as restrictive
  for all to authenticated
  using (bucket_id <> 'kitchen-photos' or shiyu_private.photo_path_allowed(name))
  with check (bucket_id <> 'kitchen-photos' or shiyu_private.photo_path_allowed(name));
create policy kitchen_photos_no_update on storage.objects as restrictive
  for update to authenticated
  using (bucket_id <> 'kitchen-photos') with check (bucket_id <> 'kitchen-photos');
create policy kitchen_photos_no_anonymous on storage.objects as restrictive
  for all to anon
  using (bucket_id <> 'kitchen-photos') with check (bucket_id <> 'kitchen-photos');

commit;
