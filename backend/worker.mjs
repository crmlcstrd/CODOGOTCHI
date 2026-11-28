import { createClient } from "@supabase/supabase-js";
import crypto from "node:crypto";

const EYES = new Set([1,4,7,8,11,12,15,17,19,20,22,24,25,27,29,30,32,35]);
const MOUTH = new Set([2,3,5,6,9,10,13,14,16,18,21,23,26,28,31,33,34,36]);
const sha = s => crypto.createHash("sha256").update(s).digest("hex");
const sleep = ms => new Promise(r => setTimeout(r, ms));
const dayKey = (ms, tz) => new Intl.DateTimeFormat("en-CA", { timeZone: tz, year: "numeric", month: "2-digit", day: "2-digit" }).format(new Date(ms));
const localHour = tz => +new Intl.DateTimeFormat("en-GB", { timeZone: tz, hour: "2-digit", hour12: false }).format(new Date());

// scoring.ts port (constants verified against your repo)
const MAX_LEVEL = 50;
const coinsForRating = r => { r ??= 800; return r < 1100 ? 10 : r < 1500 ? 20 : 30; };
const weightForRating = r => { r ??= 800; return r < 1100 ? 1 : r < 1500 ? 1.75 : r < 1900 ? 2.5 : 3.5; };
const xpForLevel = L => Math.round(40 * L + 6 * L * L);
function levelFromXp(xp) { let L = 0; while (L < MAX_LEVEL && xp >= xpForLevel(L + 1)) L++;
  if (L >= MAX_LEVEL) return { level: MAX_LEVEL, levelProgress: 100 };
  const a = xpForLevel(L), b = xpForLevel(L + 1);
  return { level: L, levelProgress: Math.max(0, Math.min(99, Math.round(((xp - a) / Math.max(1, b - a)) * 100))) }; }
function difficultyGain(S) { const r = S.filter(s => s.counted && s.rating > 0).sort((a, b) => Date.parse(a.solved_at) - Date.parse(b.solved_at));
  if (r.length < 2) return 0; const n = r.length, avg = a => a.reduce((s, x) => s + (x.rating ?? 800), 0) / a.length;
  const sa = avg(r.slice(0, Math.min(5, Math.ceil(n / 3)))), ra = avg(r.slice(-Math.min(10, Math.ceil(n / 2)))), pk = r.reduce((m, x) => Math.max(m, x.rating ?? 800), 800);
  return Math.max(0, Math.round(Math.max(ra - sa, (pk - sa) * 0.6))); }

let db;
// Increment celebrate_seq. Prefer the atomic SQL RPC; if it is missing/broken (older DB setup),
// fall back to a direct read-modify-write so solves/pushes/celebrate STILL animate on the robot.
async function bump(uid, n = 1) {
  const { error } = await db.rpc("bump_seq", { uid, n });
  if (!error) return;
  console.error("bump_seq RPC failed, using fallback:", error.message);
  const { data: st, error: e2 } = await db.from("buddy_state").select("celebrate_seq").eq("user_id", uid).single();
  if (e2 || !st) { console.error("bump fallback read failed:", e2?.message); return; }
  const { error: e3 } = await db.from("buddy_state")
    .update({ celebrate_seq: Number(st.celebrate_seq ?? 0) + n }).eq("user_id", uid);
  if (e3) console.error("bump fallback write failed:", e3.message);
}

async function metricsFor(u) {
  const now = Date.now(), tz = u.timezone || "UTC", today = dayKey(now, tz);
  const [{ data: S }, { data: P }, { data: st }] = await Promise.all([
    db.from("solves").select("rating,counted,coins,xp,solved_at").eq("user_id", u.id),
    db.from("pushes").select("counted,coins,xp,pushed_at").eq("user_id", u.id),
    db.from("buddy_state").select("*").eq("user_id", u.id).single(),
  ]);
  if (!st) throw new Error("buddy_state row missing for user " + u.id);
  const s = S || [], p = P || [];
  const earned = s.reduce((a, x) => a + x.coins, 0) + p.reduce((a, x) => a + x.coins, 0);
  const gain = difficultyGain(s);
  const totalXp = Math.round(s.reduce((a, x) => a + +x.xp, 0) + p.reduce((a, x) => a + x.xp, 0) + Math.round(gain / 15));
  const { level, levelProgress } = levelFromXp(totalXp);
  const tS = s.filter(x => x.counted && dayKey(Date.parse(x.solved_at), tz) === today);
  const tP = p.filter(x => x.counted && dayKey(Date.parse(x.pushed_at), tz) === today).length;
  const score = tP + tS.reduce((a, x) => a + weightForRating(x.rating), 0);
  const mood = score >= 5 ? 3 : score >= 2.5 ? 2 : score >= 1 ? 1 : 0;
  const cutoff = dayKey(now - 14 * 864e5, tz);
  const days = new Set([...s, ...p].map(x => dayKey(Date.parse(x.solved_at || x.pushed_at), tz)).filter(d => d >= cutoff)).size;
  const bonus = mood === 3 ? 18 : mood === 2 ? 12 : mood === 1 ? 6 : 0;
  const health = Math.max(10, Math.min(100, Math.round((Math.min(14, days) / 14) * 82) + bonus));
  const h = localHour(tz), qs = u.quiet_start, qe = u.quiet_end;
  const sleepNow = qs === qe ? false : qs > qe ? (h >= qs || h < qe) : (h >= qs && h < qe);
  return { earned, balance: Math.max(0, earned - st.spent_coins), level, levelProgress, totalXp, mood, health,
    todaySolves: tS.length, todayPushes: tP, celebrateSeq: Number(st.celebrate_seq), petSeq: Number(st.pet_seq ?? 0),
    eyes: st.equipped_eyes, mouth: st.equipped_mouth, sleep: sleepNow, st,
    coinsFromCf: s.reduce((a, x) => a + x.coins, 0),
    coinsFromGithub: p.reduce((a, x) => a + x.coins, 0),
    difficultyGain: gain,
    todayWeightedScore: +score.toFixed(1),
    newSolvesCount: s.filter(x => x.counted).length,
    newGithubPushesCount: p.filter(x => x.counted).length,
    spentCoins: st.spent_coins };
}

// ---- ledger writers (no daily cap; dedup is the anti-replay guard) ----
// One shared key for webhook + events poll so the same push can never pay twice:
const pushKey = (repo, ref, head) => `push-${repo || "?"}-${ref || "?"}-${head}`;
async function recordPush(u, k, ts, repo) {
  const { data: ins } = await db.from("pushes").upsert(
    { user_id: u.id, dedup_key: k, pushed_at: new Date(ts).toISOString(), repo, counted: true, coins: 15, xp: 20 },
    { onConflict: "user_id,dedup_key", ignoreDuplicates: true }).select("id");
  if (ins?.length) await bump(u.id);
}
const okSub = s => s.verdict === "OK" && ["PRACTICE", "CONTESTANT", "VIRTUAL"].includes(s.author?.participantType);
async function cfFetch(h, c) {
  const r = await fetch(`https://codeforces.com/api/user.status?handle=${encodeURIComponent(h)}&from=1&count=${c}`);
  if (r.status === 503) throw new Error("cf-limit");
  const j = await r.json(); return j.status === "OK" ? j.result : [];
}
async function backfillCF(u) {
  if (!u.cf_handle) return db.from("buddy_state").update({ baseline_done: true }).eq("user_id", u.id);
  const subs = await cfFetch(u.cf_handle, 1500);
  const rows = subs.filter(okSub).map(s => ({ user_id: u.id, problem_key: `${s.problem.contestId ?? "gym"}-${s.problem.index}`,
    rating: s.problem.rating ?? null, solved_at: new Date(s.creationTimeSeconds * 1000).toISOString(), counted: false, coins: 0, xp: 0 }));
  if (rows.length) await db.from("solves").upsert(rows, { onConflict: "user_id,problem_key", ignoreDuplicates: true });
  await db.from("buddy_state").update({ baseline_done: true }).eq("user_id", u.id);
}
async function pollCF(u) {
  const subs = await cfFetch(u.cf_handle, 30);
  const rows = subs.filter(okSub).map(s => { const r = s.problem.rating ?? null;
    return { user_id: u.id, problem_key: `${s.problem.contestId ?? "gym"}-${s.problem.index}`, rating: r,
      solved_at: new Date(s.creationTimeSeconds * 1000).toISOString(), counted: true, coins: coinsForRating(r), xp: weightForRating(r) * 25 }; });
  if (!rows.length) return;
  const { data: ins } = await db.from("solves").upsert(rows, { onConflict: "user_id,problem_key", ignoreDuplicates: true }).select("counted");
  const n = (ins || []).filter(x => x.counted).length; if (n) await bump(u.id, n);
}
async function pollGH(u, env) {
  const headers = { "User-Agent": "Codogotchi/1.0", Accept: "application/vnd.github+json" };
  if (env.GH_TOKEN) headers.Authorization = `Bearer ${env.GH_TOKEN}`;
  const r = await fetch(`https://api.github.com/users/${encodeURIComponent(u.gh_handle)}/events/public?per_page=100`, { headers });
  if (!r.ok) return;
  const rows = (await r.json()).filter(ev => ev.type === "PushEvent").map(ev => ({
    user_id: u.id,
    dedup_key: ev.payload?.head ? pushKey(ev.repo?.name, ev.payload?.ref, ev.payload.head) : `ev-${ev.id}`,
    pushed_at: new Date(Date.parse(ev.created_at)).toISOString(),
    repo: ev.repo?.name, counted: true, coins: 15, xp: 20 }));
  if (!rows.length) return;
  const { data: ins } = await db.from("pushes").upsert(rows, { onConflict: "user_id,dedup_key", ignoreDuplicates: true }).select("id");
  if (ins?.length) await bump(u.id, ins.length);
}
async function activeUsers() {
  const { data } = await db.from("devices").select("user_id").gt("last_seen_at", new Date(Date.now() - 864e5).toISOString());
  const ids = [...new Set((data || []).map(d => d.user_id))]; if (!ids.length) return [];
  const { data: us } = await db.from("users").select("*").in("id", ids); return us || [];
}
let cfBackoff = 0;
async function tick(env) {
  for (const u of await activeUsers()) {
    try {
      if (u.cf_handle && Date.now() >= cfBackoff) {
        const { data: st } = await db.from("buddy_state").select("baseline_done").eq("user_id", u.id).single();
        if (!st?.baseline_done) await backfillCF(u); else await pollCF(u);
      }
      if (u.gh_handle) await pollGH(u, env);
    } catch (e) { if (e.message === "cf-limit") cfBackoff = Date.now() + 120000; }
    await sleep(2000); // CF budget ~1 req / 2 s
  }
}

// Inline sync when the device polls (throttled; ledger dedup makes any overlap harmless)
const lastReact = new Map();   // per user+kind throttle for website pet/celebrate (best effort, per isolate)
const lastSync = new Map();
async function syncUser(u, env) {
  const t = lastSync.get(u.id) || 0;
  if (Date.now() - t < 25000) return;
  lastSync.set(u.id, Date.now());
  try {
    if (u.cf_handle && Date.now() >= cfBackoff) {
      const { data: st } = await db.from("buddy_state").select("baseline_done").eq("user_id", u.id).single();
      if (st?.baseline_done) await pollCF(u);   // heavy 1500-backfill stays with cron/pairing
    }
    if (u.gh_handle) await pollGH(u, env);
  } catch (e) { if (e.message === "cf-limit") cfBackoff = Date.now() + 120000; }
}

const CORS = { "Access-Control-Allow-Origin": "*", "Access-Control-Allow-Headers": "Authorization,Content-Type,x-dev-secret", "Access-Control-Allow-Methods": "GET,POST,PATCH,OPTIONS" };
const json = (o, s = 200) => new Response(JSON.stringify(o), { status: s, headers: { "content-type": "application/json", ...CORS } });

export default {
  async fetch(req, env, ctx) {
    db = createClient(env.SUPABASE_URL, env.SUPABASE_SERVICE_KEY);
    if (req.method === "OPTIONS") return new Response(null, { headers: CORS });
    const { pathname } = new URL(req.url);
    const auth = async () => {
    const t = (req.headers.get("authorization") || "").replace(/^Bearer /, ""); if (!t) return null;
    const { data: d } = await db.from("devices").select("id,user_id,last_seen_at").eq("token_hash", sha(t)).maybeSingle(); if (!d) return null;
    const { data: u } = await db.from("users").select("*").eq("id", d.user_id).single();
    // Device polls every ~2 s; heartbeat at most once a minute so we don't hammer the DB with writes.
    if (u && (!d.last_seen_at || Date.now() - Date.parse(d.last_seen_at) > 60000))
    await db.from("devices").update({ last_seen_at: new Date().toISOString() }).eq("id", d.id);
    return u;
    };
    try {
      if (pathname === "/api/device/state") {
        const u = await auth(); if (!u) return json({ error: "unauthorized" }, 401);
        ctx.waitUntil(syncUser(u, env)); // ingest in the background (throttled 25 s) so polls answer fast
        const m = await metricsFor(u);
        const body = JSON.stringify({ v: 1, mood: m.mood, health: m.health, level: m.level, eyes: m.eyes, mouth: m.mouth, celebrate_seq: m.celebrateSeq, pet_seq: m.petSeq, sleep: m.sleep });
        const tag = '"' + sha(body).slice(0, 16) + '"';
        if (req.headers.get("if-none-match") === tag) return new Response(null, { status: 304, headers: { ETag: tag, ...CORS } });
        return new Response(body, { headers: { "content-type": "application/json", ETag: tag, ...CORS } });
      }
      if (pathname === "/api/pair" && req.method === "POST") {
        const b = await req.json();
        if (!b.codeforcesHandle && !b.githubHandle) return json({ error: "need a handle" }, 400);
        const ins = await db.from("users").insert({
          user_name: (b.userName || "").trim(), buddy_name: (b.buddyName || "").trim(),
          cf_handle: (b.codeforcesHandle || "").trim(), gh_handle: (b.githubHandle || "").trim(),
          timezone: b.timezone || "UTC", quiet_start: b.quietStart ?? 23, quiet_end: b.quietEnd ?? 7
        }).select().single();
        if (ins.error) return json({ step: "users.insert", error: ins.error.message, code: ins.error.code, hint: ins.error.hint }, 500);
        const u = ins.data;
        const bs = await db.from("buddy_state").insert({ user_id: u.id });
        if (bs.error) return json({ step: "buddy_state.insert", error: bs.error.message }, 500);
        const token = crypto.randomBytes(32).toString("base64url");
        const dv = await db.from("devices").insert({ user_id: u.id, token_hash: sha(token) });
        if (dv.error) return json({ step: "devices.insert", error: dv.error.message }, 500);
        const { data: starters } = await db.from("accessory_catalog").select("id,slot").eq("is_starter", true);
        if (starters?.length) await db.from("unlocks").upsert(starters.map(s => ({ user_id: u.id, accessory_id: s.id })), { onConflict: "user_id,accessory_id", ignoreDuplicates: true });
        const sid = +b.starterId || 0;
        const pick = starters?.find(s => s.id === sid);
        if (pick) await db.from("buddy_state").update(pick.slot === 0 ? { equipped_eyes: sid } : { equipped_mouth: sid }).eq("user_id", u.id);
        ctx.waitUntil(backfillCF(u));
        return json({ userId: u.id, token });
      }
      if (pathname === "/api/me") {
        const u = await auth(); if (!u) return json({ error: "unauthorized" }, 401);
        if (req.method === "PATCH") {
          const b = await req.json(), patch = {};
          for (const k of ["user_name", "buddy_name", "cf_handle", "gh_handle", "timezone"]) if (b[k] != null) patch[k] = String(b[k]).trim();
          if (b.quiet_start != null) patch.quiet_start = b.quiet_start;
          if (b.quiet_end != null) patch.quiet_end = b.quiet_end;
          const changed = (patch.cf_handle && patch.cf_handle !== u.cf_handle) || (patch.gh_handle && patch.gh_handle !== u.gh_handle);
          await db.from("users").update(patch).eq("id", u.id);
          if (changed) { await db.from("buddy_state").update({ baseline_done: false }).eq("user_id", u.id); ctx.waitUntil(backfillCF({ ...u, ...patch })); }
          return json({ ok: true });
        }
        const m = await metricsFor(u);
        const { data: owned } = await db.from("unlocks").select("accessory_id").eq("user_id", u.id);
        return json({ user: u, metrics: m, owned: (owned || []).map(o => o.accessory_id) });
      }
      if (pathname === "/api/shop") {
        const u = await auth(); if (!u) return json({ error: "unauthorized" }, 401);
        const m = await metricsFor(u);
        const { data: cat } = await db.from("accessory_catalog").select("*").order("unlock_level");
        const { data: owned } = await db.from("unlocks").select("accessory_id").eq("user_id", u.id);
        const own = new Set((owned || []).map(o => o.accessory_id));
        return json({ balance: m.balance, level: m.level, items: cat.map(c => ({ ...c, owned: own.has(c.id), affordable: m.balance >= c.price, levelLocked: m.level < c.unlock_level })) });
      }
      if (pathname === "/api/shop/purchase" && req.method === "POST") {
        const u = await auth(); if (!u) return json({ error: "unauthorized" }, 401);
        const id = +(await req.json()).accessoryId;
        const { data: item } = await db.from("accessory_catalog").select("*").eq("id", id).maybeSingle();
        if (!item) return json({ error: "unknown item" }, 404);
        const m = await metricsFor(u);
        if (m.level < item.unlock_level) return json({ error: "level locked" }, 403);
        if (m.balance < item.price) return json({ error: "insufficient coins" }, 403);
        const { data: ins } = await db.from("unlocks").upsert({ user_id: u.id, accessory_id: id }, { onConflict: "user_id,accessory_id", ignoreDuplicates: true }).select();
        if (!ins?.length) return json({ error: "already owned" }, 409);
        await db.from("buddy_state").update({ spent_coins: m.st.spent_coins + item.price }).eq("user_id", u.id);
        return json({ ok: true, balance: m.balance - item.price });
      }
      if (pathname === "/api/shop/equip" && req.method === "POST") {
        const u = await auth(); if (!u) return json({ error: "unauthorized" }, 401);
        const b = await req.json(); const id = b.accessoryId == null ? 0 : +b.accessoryId;
        if (id !== 0) {
          const slot = EYES.has(id) ? 0 : MOUTH.has(id) ? 1 : -1;
          if (slot < 0) return json({ error: "bad slot" }, 400);
          const { data: own } = await db.from("unlocks").select("accessory_id").eq("user_id", u.id).eq("accessory_id", id).maybeSingle();
          if (!own) return json({ error: "not owned" }, 403);
          await db.from("buddy_state").update(slot === 0 ? { equipped_eyes: id } : { equipped_mouth: id }).eq("user_id", u.id);
        } else {
          await db.from("buddy_state").update({ [b.slot === "mouth" ? "equipped_mouth" : "equipped_eyes"]: 0 }).eq("user_id", u.id);
        }
        await bump(u.id);
        return json({ ok: true });
      }
      // Website "Pet" / "Celebrate" buttons -> the robot. The device polls /api/device/state and reacts when
      // celebrate_seq / pet_seq go up (celebrate reuses the same counter a real solve/push bumps).
      if ((pathname === "/api/celebrate" || pathname === "/api/pet") && req.method === "POST") {
        const u = await auth(); if (!u) return json({ error: "unauthorized" }, 401);
        const kind = pathname === "/api/pet" ? "pet" : "celebrate", key = u.id + kind;
        if (Date.now() - (lastReact.get(key) || 0) < 1500) return json({ ok: true, throttled: true });
        lastReact.set(key, Date.now());
        if (kind === "celebrate") { await bump(u.id); return json({ ok: true }); }
        const { data: st, error } = await db.from("buddy_state").select("pet_seq").eq("user_id", u.id).single();
        if (error || !st) return json({ error: "Robot DB needs a one-time update: run backend/migrations/001_pet_seq.sql in the Supabase SQL editor, then pet again." }, 501);
        await db.from("buddy_state").update({ pet_seq: Number(st.pet_seq) + 1 }).eq("user_id", u.id);
        return json({ ok: true });
      }
      if (pathname === "/api/dev/grant" && req.method === "POST") {
        if (req.headers.get("x-dev-secret") !== env.DEV_SECRET) return json({ error: "forbidden" }, 403);
        const { userId, coins = 0, xp = 0, celebrate = false } = await req.json();
        if (coins || xp) await db.from("pushes").insert({ user_id: userId, dedup_key: `grant-${crypto.randomUUID()}`, pushed_at: new Date().toISOString(), repo: "grant", counted: true, coins, xp });
        if (coins || xp || celebrate) await bump(userId);
        return json({ ok: true });
      }
      if (pathname === "/api/webhooks/github" && req.method === "POST") {
        const ab = await req.arrayBuffer();
        const sig = "sha256=" + crypto.createHmac("sha256", env.GH_WEBHOOK_SECRET).update(Buffer.from(ab)).digest("hex");
        const got = req.headers.get("x-hub-signature-256") || "";
        if (got.length !== sig.length || !crypto.timingSafeEqual(Buffer.from(got), Buffer.from(sig))) return json({ error: "bad sig" }, 401);
        const delivery = req.headers.get("x-github-delivery");
        const { data: seen } = await db.from("webhook_deliveries").upsert({ delivery_id: delivery }, { onConflict: "delivery_id", ignoreDuplicates: true }).select();
        if (!seen?.length) return json({ ok: true }); // replay
        if (req.headers.get("x-github-event") !== "push") return json({ ok: true });
        const p = JSON.parse(Buffer.from(ab).toString());
        if (!p.commits?.length) return json({ ok: true });
        const author = (p.head_commit?.author?.username || p.sender?.login || "").toLowerCase();
        const { data: u } = await db.from("users").select("*").ilike("gh_handle", author).maybeSingle();
        if (u && p.after) await recordPush(u, pushKey(p.repository?.full_name, p.ref, p.after), Date.now(), p.repository?.full_name);
        return json({ ok: true });
      }
      if (pathname === "/api/internal/poll" && req.method === "POST") {
        if (req.headers.get("x-dev-secret") !== env.DEV_SECRET) return json({ error: "forbidden" }, 403);
        ctx.waitUntil(tick(env)); return json({ ok: true });
      }
      if (pathname === "/api/debug/db") {
        if (req.headers.get("x-dev-secret") !== env.DEV_SECRET) return json({ error: "forbidden" }, 403);
        const k = env.SUPABASE_SERVICE_KEY || "";
        let keyKind = "unrecognized";
        if (k.startsWith("sb_secret_")) keyKind = "secret (correct)";
        else if (k.startsWith("sb_publishable_")) keyKind = "publishable (WRONG - use secret/service_role)";
        else { try { keyKind = "jwt role=" + JSON.parse(atob(k.split(".")[1].replace(/-/g, "+").replace(/_/g, "/"))).role; } catch {} }
        const t = await db.from("users").select("id").limit(1);
        const c = await db.from("accessory_catalog").select("id");
        return json({
          project: env.SUPABASE_URL,
          keyKind,
          usersTable: t.error ? t.error.message : "ok",
          catalogRows: c.error ? c.error.message : (c.data || []).length
        });
      }
      if (pathname === "/api/sync" && req.method === "POST") {
        const u = await auth(); if (!u) return json({ error: "unauthorized" }, 401);
        lastSync.delete(u.id);            // force ingest now for this user
        await syncUser(u, env);
        return json({ metrics: await metricsFor(u) });
      }
      return json({ error: "not found" }, 404);
    } catch (e) { return json({ error: String(e?.message || e) }, 500); }
  },
  async scheduled(ev, env, ctx) {
    db = createClient(env.SUPABASE_URL, env.SUPABASE_SERVICE_KEY);
    ctx.waitUntil(tick(env));
  },
};