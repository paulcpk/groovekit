// Jumping from an automation clip to the control it drives: the view that holds it opens, and
// the control flashes for a moment.

import type { AutomationTarget, Project } from "@groovekit/schema/project";
import { create } from "zustand";
import { automationKey } from "./automation";
import { openWindow } from "@/state/windows";
import { setUi } from "@/state/ui";

const useRevealed = create<{ key?: string; at: number }>(() => ({ at: 0 }));

/** Opens what holds the control for a target (the channel rack, the mixer, a sampler's or an
    effect's window) and marks the control, so it flashes. */
export function revealControl(target: AutomationTarget, doc: Project) {
  if (target.scope === "channel") {
    const channel = doc.channels.find((c) => c.id === target.channel);
    setUi({ selectedChannel: target.channel, bottomOpen: true, bottomPanel: "rack" });
    // Volume and pan are on the rack row; the filter's knobs are in the sampler's window.
    if (channel && target.param !== "volume" && target.param !== "pan") openWindow({ kind: "channel", channel: channel.id });
  } else {
    setUi({ selectedInsert: target.insert, bottomOpen: true, bottomPanel: "mixer" });
    if (target.scope === "effect" && target.param !== "mix" && target.insert && target.effect) {
      const effect = doc.mixer.inserts.find((i) => i.id === target.insert)?.effects?.find((e) => e.id === target.effect);
      if (effect?.type === "builtin") openWindow({ kind: "effect", insert: target.insert, effect: target.effect });
    }
  }
  // After the views have opened, so the control is there to flash.
  requestAnimationFrame(() => useRevealed.setState({ key: automationKey(target), at: Date.now() }));
}

/** Whether a control with this target was just revealed: the moment it was, to key a flash on. */
export function useRevealedAt(target: AutomationTarget | undefined) {
  const key = target ? automationKey(target) : undefined;
  return useRevealed((s) => (key !== undefined && s.key === key && Date.now() - s.at < 2500 ? s.at : undefined));
}
