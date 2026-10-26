import { EffectWindow } from "@/features/mixer/EffectWindow";
import { AudioWindow } from "@/features/sampler/AudioWindow";
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
      if (channel?.instrument.type === "sampler") return <SamplerWindow key={w.key} window={w} channel={channel} sampler={channel.instrument} />;
      if (channel?.instrument.type === "audio") return <AudioWindow key={w.key} window={w} channel={channel} audio={channel.instrument} />;
      return null;
    }
    const insert = doc?.mixer.inserts.find((i) => i.id === spec.insert);
    const effect = insert?.effects?.find((e) => e.id === spec.effect);
    return insert && effect?.type === "builtin" ? <EffectWindow key={w.key} window={w} insert={insert} effect={effect} /> : null;
  });

  useEffect(() => {
    if (doc) windows.forEach((w, i) => !found[i] && closeWindow(w.key));
  });

  // Above the app, below menus and dialogs.
  return <div className="pointer-events-none fixed inset-0 isolate z-30">{found}</div>;
}
