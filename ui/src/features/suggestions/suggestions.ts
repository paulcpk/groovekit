// Sounds the engine picks out for the song in the background (suggestions.*): what's ready, for the
// browser's "For this song" folder, the settings and the agent's view of the app. On by default:
// the first time an engine says the setting was never made, the app turns it on. Each list holds two
// rounds of the number of sounds the agent offers to pick from.

import type { Suggestions } from "@groovekit/mcp/src/engine.ts";
import { create } from "zustand";
import { useAgentChoices } from "@/features/agent/agent";
import { engine, listen, useEngine } from "@/state/engine";

export type { Suggestions };

export const useSuggestions = create<{ status?: Suggestions; full?: Suggestions; wantFull: boolean }>(() => ({ wantFull: false }));

let fetching = false;
let again = false;

/** What's ready (and with wantFull, every list in full), asked again after each change. */
async function refresh() {
  if (useEngine.getState().status !== "connected") return;
  if (fetching) {
    again = true;
    return;
  }
  fetching = true;
  try {
    const { wantFull } = useSuggestions.getState();
    const result = await engine.call("suggestions.get", wantFull ? { full: true } : {});
    if (!result.chosen) {
      // Never set on this Mac: on, with two rounds of as many as the agent offers.
      const configured = await engine.call("suggestions.configure", { enabled: true, choices: useAgentChoices.getState().choices });
      useSuggestions.setState({ status: configured });
    } else {
      useSuggestions.setState(wantFull ? { status: result, full: result } : { status: result });
    }
  } catch {
    // An engine without suggestions (older) has none.
    useSuggestions.setState({ status: undefined, full: undefined });
  } finally {
    fetching = false;
    if (again) {
      again = false;
      void refresh();
    }
  }
}

/** Turns the background work on or off. */
export async function setSuggestionsEnabled(enabled: boolean) {
  const status = await engine.call("suggestions.configure", { enabled, choices: useAgentChoices.getState().choices });
  useSuggestions.setState({ status });
  void refresh();
}

/** Every list in full while the browser shows them. */
export function wantFullSuggestions(want: boolean) {
  if (useSuggestions.getState().wantFull === want) return;
  useSuggestions.setState({ wantFull: want });
  if (want) void refresh();
}

let timer: ReturnType<typeof setTimeout> | undefined;
function soon() {
  clearTimeout(timer);
  timer = setTimeout(() => void refresh(), 150);
}

/** Keeps what's ready up to date while the app runs; returns how to stop. */
export function syncSuggestions() {
  const stops = [
    listen("suggestions", soon),
    listen("snapshot", soon),
    useEngine.subscribe((s, before) => {
      if (s.status === "connected" && before.status !== "connected") soon();
    }),
    useAgentChoices.subscribe((s, before) => {
      if (s.choices !== before.choices && useSuggestions.getState().status?.enabled) {
        void engine.call("suggestions.configure", { choices: s.choices }).then(() => soon(), () => {});
      }
    }),
  ];
  soon();
  return () => stops.forEach((stop) => stop());
}

/** For the agent's view of the app: which lists are ready, in a line; empty when there are none. */
export function suggestionsForAgent() {
  const status = useSuggestions.getState().status;
  if (!status?.enabled || !status.lists.length) return "";
  const ready = status.lists.filter((l) => l.count > 0);
  if (!ready.length) return "";
  const missing = ready.filter((l) => !l.inSong).map((l) => l.label.toLowerCase());
  const working = status.state === "finding" || status.state === "trying" ? ", still trying some in the song" : "";
  return `sounds ready for this song in suggested_sounds: ${ready.length} lists${missing.length ? ` (for parts it doesn't have yet: ${missing.join(", ")})` : ""}${working}`;
}
