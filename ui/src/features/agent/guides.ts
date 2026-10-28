// Style guides: how to make each style, kept by the engine (built in, with the user's own versions
// and additions in the app data folder). A song's style names one; the agent reads it before it
// works. "guides" notifications keep the list current when anyone changes a guide.

import { useEffect } from "react";
import { create } from "zustand";
import { call, engine, listen, useEngine } from "@/state/engine";

export interface GuideSummary {
  id: string;
  name: string;
  summary: string;
  builtIn: boolean;
  /** The user's own version (of a built-in guide), or a guide of their own. */
  edited: boolean;
  /** The open song's style. */
  song: boolean;
  /** A built-in guide the user took out of the list. */
  hidden?: boolean;
}

export interface Guide extends Omit<GuideSummary, "song"> {
  text: string;
  targets: Record<string, unknown>;
}

export const useGuides = create<{ guides: GuideSummary[]; loaded: boolean }>(() => ({ guides: [], loaded: false }));

listen("guides", (params: { guides: GuideSummary[] }) => useGuides.setState({ guides: params.guides, loaded: true }));

export async function refreshGuides() {
  const r = await engine.call<{ guides: GuideSummary[] }>("guides.list");
  useGuides.setState({ guides: r.guides, loaded: true });
  return r.guides;
}

/** The guides, fetched when the engine connects and when the song or its style changes. */
export function useStyleGuides() {
  const connected = useEngine((s) => s.status === "connected");
  const project = useEngine((s) => s.project);
  const style = useEngine((s) => s.doc?.style);

  useEffect(() => {
    if (connected) void refreshGuides().catch(() => {});
  }, [connected, project, style]);

  return useGuides();
}

export const getGuide = (id: string) => call<Guide>("guides.get", { id });

export const saveGuide = (text: string, id?: string) => call<Guide>("guides.save", { text, ...(id ? { id } : {}) });

/** Out of the list: one of the user's own is deleted, a built-in one hidden. */
export const deleteGuide = (id: string) => call<{ removed: "deleted" | "hidden" }>("guides.delete", { id });

export const unhideGuide = (id: string) => call("guides.unhide", { id });

export const removeGuide = (id: string) => call<{ removed: "nothing" | "restored" | "deleted" }>("guides.remove", { id });

/** The song's guide, as a part of the agent's instructions; empty when the song has no style. */
export async function guideForAgent() {
  const style = useEngine.getState().doc?.style;
  if (!style) return "";
  try {
    const guide = await engine.call<Guide>("guides.get", { id: style });
    return `The song's style is ${guide.name}. Its guide, which you follow (the user's notes win where they disagree):\n\n${guide.text.trim()}`;
  } catch {
    return "";
  }
}
