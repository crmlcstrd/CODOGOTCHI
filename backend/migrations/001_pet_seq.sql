-- CODOGOTCHI one-time DB fix (idempotent). Run in Supabase SQL editor.

-- Website "Pet" counter (the worker's /api/pet endpoint needs this column).
alter table public.buddy_state
  add column if not exists pet_seq bigint not null default 0;

-- Celebration counter (solve / push / celebrate-button). Add only if missing.
alter table public.buddy_state
  add column if not exists celebrate_seq bigint not null default 0;

-- Atomic increment used by the worker after every solve/purchase/celebrate.
drop function if exists public.bump_seq;
create function public.bump_seq(uid public.buddy_state.user_id%TYPE, n integer default 1)
returns void
language sql
security definer
set search_path = public
as $$
  update public.buddy_state
     set celebrate_seq = celebrate_seq + n
   where user_id = uid;
$$;

notify pgrst, 'reload schema';