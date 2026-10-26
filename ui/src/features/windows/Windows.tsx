import { SamplerWindow } from "@/features/sampler/SamplerWindow";
import { useEngine } from "@/state/engine";
import { closeWindow, useWindows } from "@/state/windows";
import { useEffect } from "react";

/** The open floating windows. A window whose channel or effect is gone closes. */
export function Windows() {
  const windows = useWindows((s) => s.windows);
  const doc = useEngine((s) => s.doc);

  const found = windows.map((w) => {
    const spec = w.spec;
    if (spec.kind === "channel") {
      const channel = doc?.channels.find((c) => c.id === spec.channel);
      return channel?.instrument.type === "sampler" ? <SamplerWindow key={w.key} window={w} channel={channel} sampler={channel.instrument} /> : null;
    }
    return null;
  });

  useEffect(() => {
    if (doc) windows.forEach((w, i) => !found[i] && closeWindow(w.key));
  });

  // Above the app, below menus and dialogs.
  return <div className="pointer-events-none fixed inset-0 isolate z-30">{found}</div>;
}
