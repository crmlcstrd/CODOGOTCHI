import { useCallback, useEffect, useRef, useState } from "react";
import { useHydrated } from "@tanstack/react-router";
import { useServerFn } from "@tanstack/react-start";
import { toast } from "sonner";
import { ACCESSORIES, getAccessoryById } from "./accessories";
import { syncExternalProfile, type SyncedProfileData } from "./integrations.functions";
import type { BuddyMetrics } from "./scoring";
import { moodLabelFromLevel } from "./scoring";
import { api, getToken, setToken } from "./api";

export const STORAGE_KEY = "codogotchi_state_v2"; // legacy: read-only fallback for handles + analytics
const ANALYTICS_KEY = "codogotchi_analytics_v1";
const ANALYTICS_TTL = 5 * 60 * 1000;

export interface BuddyState {
  onboarded: boolean;
  userName: string;
  buddyName: string;
  codeforcesHandle: string;
  githubHandle: string;
  ownedIds: string[];
  equippedEyesId: string | null;
  equippedMouthId: string | null;
  spentCoins: number;
  baseline: SyncedProfileData["baseline"] | null;
  profileData: SyncedProfileData | null;
}

const ZERO_METRICS: BuddyMetrics = {
  totalEarnedCoins: 0, coinsFromCf: 0, coinsFromGithub: 0, level: 0, levelProgress: 0, totalXp: 0,
  health: 60, mood: 1, moodLabel: "Neutral", todayWeightedScore: 0, todaySolves: 0, todayPushes: 0,
  newSolvesCount: 0, newGithubPushesCount: 0, celebrateSeq: 0, difficultyGain: 0,
};

const DEFAULT_STATE: BuddyState = {
  onboarded: false, userName: "", buddyName: "", codeforcesHandle: "", githubHandle: "",
  ownedIds: [], equippedEyesId: null, equippedMouthId: null, spentCoins: 0, baseline: null, profileData: null,
};

interface ServerMetrics {
  earned: number; balance: number; level: number; levelProgress: number; totalXp: number;
  mood: 0 | 1 | 2 | 3; health: number; todaySolves: number; todayPushes: number;
  celebrateSeq: number; eyes: number; mouth: number; sleep: boolean;
  coinsFromCf: number; coinsFromGithub: number; difficultyGain: number;
  todayWeightedScore: number; newSolvesCount: number; newGithubPushesCount: number; spentCoins: number;
}
interface MeResponse {
  user: { user_name: string; buddy_name: string; cf_handle: string; gh_handle: string; timezone: string };
  metrics: ServerMetrics;
  owned: number[];
}

const idOf = (cosmeticId: number): string | null =>
  cosmeticId ? (ACCESSORIES.find((a) => a.cosmeticId === cosmeticId)?.id ?? null) : null;
const cosmeticOf = (id: string | null | undefined): number =>
  id ? (getAccessoryById(id)?.cosmeticId ?? 0) : 0;

function toBuddyMetrics(m: ServerMetrics): BuddyMetrics {
  return {
    totalEarnedCoins: m.earned, coinsFromCf: m.coinsFromCf, coinsFromGithub: m.coinsFromGithub,
    level: m.level, levelProgress: m.levelProgress, totalXp: m.totalXp, health: m.health,
    mood: m.mood, moodLabel: moodLabelFromLevel(m.mood), todayWeightedScore: m.todayWeightedScore,
    todaySolves: m.todaySolves, todayPushes: m.todayPushes, newSolvesCount: m.newSolvesCount,
    newGithubPushesCount: m.newGithubPushesCount, celebrateSeq: m.celebrateSeq, difficultyGain: m.difficultyGain,
  };
}

const emptyProfile = (): SyncedProfileData => ({
  syncedAt: new Date(0).toISOString(),
  baseline: { startedAtSec: 0, initialSolvedIds: [], initialGithubPushes: 0 },
  codeforces: null, github: null, metrics: ZERO_METRICS,
});

interface AnalyticsCache {
  key: string; syncedAt: number;
  codeforces: SyncedProfileData["codeforces"]; github: SyncedProfileData["github"];
}
const analyticsKey = (cf: string, gh: string) => `${cf}|${gh}`;
function readCache(key: string): AnalyticsCache | null {
  try {
    const c = JSON.parse(window.localStorage.getItem(ANALYTICS_KEY) ?? "null") as AnalyticsCache | null;
    return c && c.key === key && Date.now() - c.syncedAt < ANALYTICS_TTL ? c : null;
  } catch { return null; }
}
function writeCache(key: string, res: SyncedProfileData) {
  try {
    const c: AnalyticsCache = { key, syncedAt: Date.parse(res.syncedAt) || Date.now(), codeforces: res.codeforces, github: res.github };
    window.localStorage.setItem(ANALYTICS_KEY, JSON.stringify(c));
  } catch { /* quota */ }
}
function readLegacy(): { userName: string; buddyName: string; cf: string; gh: string; profile: SyncedProfileData | null } | null {
  try {
    const p = JSON.parse(window.localStorage.getItem(STORAGE_KEY) ?? "null");
    if (!p) return null;
    return {
      userName: p.userName ?? "", buddyName: p.buddyName ?? "",
      cf: p.codeforcesHandle ?? "", gh: p.githubHandle ?? "",
      profile: p.profileData ?? null,
    };
  } catch { return null; }
}

export function useBuddyStore() {
  const hydrated = useHydrated();
  const [state, setState] = useState<BuddyState>(DEFAULT_STATE);
  const [ready, setReady] = useState(false);
  const [deviceToken, setDeviceToken] = useState<string | null>(null);
  const runAnalytics = useServerFn(syncExternalProfile);
  const inflight = useRef<string | null>(null);

  const mergeAnalytics = useCallback((codeforces: SyncedProfileData["codeforces"], github: SyncedProfileData["github"], syncedAt: string) => {
    setState((prev) => {
      const base = prev.profileData ?? emptyProfile();
      return { ...prev, profileData: { ...base, codeforces, github, syncedAt } };
    });
  }, []);

  const loadAnalytics = useCallback(async (cf: string, gh: string) => {
    const key = analyticsKey(cf, gh);
    if (!cf && !gh) return;
    const cached = readCache(key);
    if (cached) { mergeAnalytics(cached.codeforces, cached.github, new Date(cached.syncedAt).toISOString()); return; }
    if (inflight.current === key) return;
    inflight.current = key;
    try {
      const res = await runAnalytics({ data: { codeforcesHandle: cf, githubHandle: gh } });
      writeCache(key, res);
      mergeAnalytics(res.codeforces, res.github, res.syncedAt);
    } catch {
      toast.error("Could not load Codeforces/GitHub analytics. Check the handles and try Sync again.");
    } finally {
      inflight.current = null;
    }
  }, [runAnalytics, mergeAnalytics]);

  const refresh = useCallback(async (): Promise<MeResponse | null> => {
    if (!getToken()) return null;
    const me = await api<MeResponse>("/api/me");
    setState((prev) => ({
      ...prev,
      onboarded: true,
      userName: me.user.user_name ?? "", buddyName: me.user.buddy_name ?? "",
      codeforcesHandle: me.user.cf_handle ?? "", githubHandle: me.user.gh_handle ?? "",
      ownedIds: me.owned.map(idOf).filter((x): x is string => !!x),
      equippedEyesId: idOf(me.metrics.eyes), equippedMouthId: idOf(me.metrics.mouth),
      spentCoins: me.metrics.spentCoins,
      profileData: {
        ...(prev.profileData ?? emptyProfile()),
        syncedAt: prev.profileData?.syncedAt ?? new Date(0).toISOString(),
        metrics: toBuddyMetrics(me.metrics),
      },
    }));
    return me;
  }, []);

  useEffect(() => {
    if (!hydrated) return;
    setDeviceToken(getToken());
    const legacy = readLegacy();
    if (legacy) {
      setState((prev) => ({
        ...prev,
        userName: prev.userName || legacy.userName,
        buddyName: prev.buddyName || legacy.buddyName,
        codeforcesHandle: prev.codeforcesHandle || legacy.cf,
        githubHandle: prev.githubHandle || legacy.gh,
        profileData: prev.profileData ?? legacy.profile ?? null,
      }));
      if (legacy.profile) writeCache(analyticsKey(legacy.cf, legacy.gh), legacy.profile);
    }
    refresh().catch(() => {}).finally(() => setReady(true));
  }, [hydrated, refresh]);

  const hasAnalytics = !!(state.profileData?.codeforces || state.profileData?.github);
  const analyticsStale = Date.now() - Date.parse(state.profileData?.syncedAt ?? new Date(0).toISOString()) > ANALYTICS_TTL;
  useEffect(() => {
    if (!ready) return;
    if (!state.codeforcesHandle && !state.githubHandle) return;
    if (hasAnalytics && !analyticsStale) return;
    void loadAnalytics(state.codeforcesHandle, state.githubHandle);
  }, [ready, state.codeforcesHandle, state.githubHandle, hasAnalytics, analyticsStale, loadAnalytics]);

  const completeOnboarding = useCallback(async (payload: {
    userName: string; buddyName: string; codeforcesHandle: string; githubHandle: string;
    starterIds: string[]; profileData: SyncedProfileData | null;
  }) => {
    const paired = await api<{ userId: string; token: string }>("/api/pair", {
      method: "POST",
      body: JSON.stringify({
        userName: payload.userName, buddyName: payload.buddyName,
        codeforcesHandle: payload.codeforcesHandle, githubHandle: payload.githubHandle,
        timezone: Intl.DateTimeFormat().resolvedOptions().timeZone || "UTC",
        starterId: cosmeticOf(payload.starterIds[0]),
      }),
    });
    setToken(paired.token); setDeviceToken(paired.token);
    await refresh();
    if (payload.profileData) {
      writeCache(analyticsKey(payload.codeforcesHandle, payload.githubHandle), payload.profileData);
      mergeAnalytics(payload.profileData.codeforces, payload.profileData.github, payload.profileData.syncedAt);
    }
  }, [refresh, mergeAnalytics]);

  const updateProfileSettings = useCallback(async (payload: {
    userName: string; buddyName: string; codeforcesHandle: string; githubHandle: string;
    profileData: SyncedProfileData | null;
  }) => {
    if (!getToken()) {
      setState((prev) => ({ ...prev, userName: payload.userName, buddyName: payload.buddyName,
        codeforcesHandle: payload.codeforcesHandle, githubHandle: payload.githubHandle }));
      if (payload.profileData) mergeAnalytics(payload.profileData.codeforces, payload.profileData.github, payload.profileData.syncedAt);
      toast.message("Saved locally. Link a device token on Profile to sync this to your robot.");
      return;
    }
    await api("/api/me", { method: "PATCH", body: JSON.stringify({
      user_name: payload.userName, buddy_name: payload.buddyName,
      cf_handle: payload.codeforcesHandle, gh_handle: payload.githubHandle,
    })});
    await refresh();
    if (payload.profileData) {
      writeCache(analyticsKey(payload.codeforcesHandle, payload.githubHandle), payload.profileData);
      mergeAnalytics(payload.profileData.codeforces, payload.profileData.github, payload.profileData.syncedAt);
    }
    void loadAnalytics(payload.codeforcesHandle, payload.githubHandle);
  }, [refresh, mergeAnalytics, loadAnalytics]);

  const setProfileData = useCallback((res: SyncedProfileData) => {
    writeCache(analyticsKey(state.codeforcesHandle, state.githubHandle), res);
    setState((prev) => ({ ...prev, profileData: {
      ...(prev.profileData ?? emptyProfile()),
      codeforces: res.codeforces, github: res.github, syncedAt: res.syncedAt,
      metrics: prev.profileData?.metrics ?? res.metrics,
    }}));
  }, [state.codeforcesHandle, state.githubHandle]);

  const syncNow = useCallback(async () => {
    if (!getToken()) { toast.error("Link your robot's device token on Profile first."); throw new Error("no token"); }
    const { metrics } = await api<{ metrics: ServerMetrics }>("/api/sync", { method: "POST" });
    await refresh();
    void loadAnalytics(state.codeforcesHandle, state.githubHandle);
    return toBuddyMetrics(metrics);
  }, [refresh, loadAnalytics, state.codeforcesHandle, state.githubHandle]);

  const buyAccessory = useCallback(async (accessoryId: string, _price: number): Promise<boolean> => {
    if (!getToken()) { toast.error("Link your robot's device token on Profile first."); return false; }
    const cid = cosmeticOf(accessoryId);
    try {
      await api("/api/shop/purchase", { method: "POST", body: JSON.stringify({ accessoryId: cid }) });
      await api("/api/shop/equip", { method: "POST", body: JSON.stringify({ accessoryId: cid }) });
      await refresh();
      return true;
    } catch (e) { toast.error((e as Error).message); return false; }
  }, [refresh]);

  const toggleEquip = useCallback(async (accessoryId: string): Promise<void> => {
    if (!getToken()) { toast.error("Link your robot's device token on Profile first."); return; }
    const acc = getAccessoryById(accessoryId); if (!acc) return;
    const equipped = acc.slot === "eyes" ? state.equippedEyesId === accessoryId : state.equippedMouthId === accessoryId;
    try {
      await api("/api/shop/equip", { method: "POST", body: JSON.stringify(
        equipped ? { accessoryId: null, slot: acc.slot } : { accessoryId: acc.cosmeticId }) });
      await refresh();
    } catch (e) { toast.error((e as Error).message); }
  }, [refresh, state.equippedEyesId, state.equippedMouthId]);

  const linkDevice = useCallback(async (token: string) => {
    setToken(token.trim()); setDeviceToken(token.trim());
    await refresh();
    void loadAnalytics(state.codeforcesHandle, state.githubHandle);
  }, [refresh, loadAnalytics, state.codeforcesHandle, state.githubHandle]);

  const earnedCoins = state.profileData?.metrics.totalEarnedCoins ?? 0;
  const coinBalance = Math.max(0, earnedCoins - state.spentCoins);
  const level = state.profileData?.metrics.level ?? 0;
  const health = state.profileData?.metrics.health ?? 60;
  const mood = state.profileData?.metrics.mood ?? 1;

  return {
    ready, state, earnedCoins, coinBalance, level, health, mood, deviceToken,
    completeOnboarding, updateProfileSettings, setProfileData, buyAccessory, toggleEquip,
    syncNow, linkDevice, refresh,
  };
}