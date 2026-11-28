import { useCallback, useEffect, useState } from "react";
import { useHydrated } from "@tanstack/react-router";
import { ACCESSORIES, getAccessoryById } from "./accessories";
import type { SyncedProfileData } from "./integrations.functions";
import type { BuddyMetrics } from "./scoring";
import { moodLabelFromLevel } from "./scoring";
import { api, getToken, setToken } from "./api";

export const STORAGE_KEY = "codogotchi_state_v2"; // legacy, no longer written

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

export function useBuddyStore() {
  const hydrated = useHydrated();
  const [state, setState] = useState<BuddyState>(DEFAULT_STATE);
  const [ready, setReady] = useState(false);
  const [deviceToken, setDeviceToken] = useState<string | null>(null);

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
        syncedAt: new Date().toISOString(),
        baseline: prev.baseline ?? { startedAtSec: 0, initialSolvedIds: [], initialGithubPushes: 0 },
        codeforces: prev.profileData?.codeforces ?? null,
        github: prev.profileData?.github ?? null,
        metrics: toBuddyMetrics(me.metrics),
      },
    }));
    return me;
  }, []);

  useEffect(() => {
    if (!hydrated) return;
    setDeviceToken(getToken());
    refresh().catch(() => {}).finally(() => setReady(true));
  }, [hydrated, refresh]);

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
      setState((prev) => ({ ...prev, profileData: prev.profileData
        ? { ...prev.profileData, codeforces: payload.profileData!.codeforces, github: payload.profileData!.github }
        : payload.profileData }));
    }
  }, [refresh]);

  const updateProfileSettings = useCallback(async (payload: {
    userName: string; buddyName: string; codeforcesHandle: string; githubHandle: string;
    profileData: SyncedProfileData | null;
  }) => {
    await api("/api/me", { method: "PATCH", body: JSON.stringify({
      user_name: payload.userName, buddy_name: payload.buddyName,
      cf_handle: payload.codeforcesHandle, gh_handle: payload.githubHandle,
    })});
    await refresh();
    if (payload.profileData) {
      setState((prev) => ({ ...prev, profileData: prev.profileData
        ? { ...prev.profileData, codeforces: payload.profileData!.codeforces, github: payload.profileData!.github }
        : payload.profileData }));
    }
  }, [refresh]);

  // analytics-only merge: NEVER overwrite server economy metrics
  const setProfileData = useCallback((res: SyncedProfileData) => {
    setState((prev) => ({ ...prev, profileData: {
      ...res, metrics: prev.profileData?.metrics ?? res.metrics,
    }}));
  }, []);

  const syncNow = useCallback(async () => {
    const { metrics } = await api<{ metrics: ServerMetrics }>("/api/sync", { method: "POST" });
    await refresh();
    return toBuddyMetrics(metrics);
  }, [refresh]);

  const buyAccessory = useCallback(async (accessoryId: string, _price: number): Promise<boolean> => {
    const cid = cosmeticOf(accessoryId);
    try {
      await api("/api/shop/purchase", { method: "POST", body: JSON.stringify({ accessoryId: cid }) });
      await api("/api/shop/equip", { method: "POST", body: JSON.stringify({ accessoryId: cid }) });
      await refresh();
      return true;
    } catch { return false; }
  }, [refresh]);

  const toggleEquip = useCallback(async (accessoryId: string): Promise<void> => {
    const acc = getAccessoryById(accessoryId); if (!acc) return;
    const equipped = acc.slot === "eyes" ? state.equippedEyesId === accessoryId : state.equippedMouthId === accessoryId;
    await api("/api/shop/equip", { method: "POST", body: JSON.stringify(
      equipped ? { accessoryId: null, slot: acc.slot } : { accessoryId: acc.cosmeticId }) });
    await refresh();
  }, [refresh, state.equippedEyesId, state.equippedMouthId]);

  const linkDevice = useCallback(async (token: string) => {
    setToken(token.trim()); setDeviceToken(token.trim());
    await refresh();
  }, [refresh]);

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