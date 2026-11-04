// What the switcher and the overview call each window, and the icon it shows by.

import { AudioWaveform, BookOpen, Gauge, LayoutDashboard, Map as MapIcon, MessageCircleQuestion, Plug, SlidersHorizontal, type LucideIcon } from "lucide-react";
import type { Project } from "@groovekit/schema/project";
import { effectNames } from "@/lib/catalogue";
import { useWindows, type WindowTarget } from "@/state/windows";

/** A plugin's window by what it belongs to: the channel it plays, or the insert and its slot. */
export function pluginLabel(key: string, doc: Project | undefined) {
  if (key.startsWith("channel:")) {
    const channel = doc?.channels.find((c) => c.id === key.slice(8));
    const plugin = channel?.instrument.type === "plugin" ? channel.instrument.plugin.name : undefined;
    return [channel?.name || key.slice(8), plugin].filter(Boolean).join(" · ");
  }
  const [insertId, effectId] = key.slice(7).split("/");
  const insert = doc?.mixer.inserts.find((i) => i.id === insertId);
  const effect = insert?.effects?.find((e) => e.id === effectId);
  const plugin = effect?.type === "plugin" ? effect.plugin.name : effectId;
  return `${insert?.name || insertId} · ${plugin}`;
}

export function targetLabel(target: WindowTarget, doc: Project | undefined): string {
  if (target === "main") return "Arrangement";
  if (target.startsWith("plugin:")) return pluginLabel(target.slice(7), doc);
  const named = useWindows.getState().names[target];
  if (named) return named;
  if (target.startsWith("effect:")) {
    const [insertId, effectId] = target.slice(7).split("/");
    const effect = doc?.mixer.inserts.find((i) => i.id === insertId)?.effects?.find((e) => e.id === effectId);
    return effect?.type === "builtin" ? effectNames[effect.kind] ?? effect.kind : effectId;
  }
  return target.replace(/^[^:]*:/, "");
}

export function targetIcon(target: WindowTarget): LucideIcon {
  if (target === "main") return LayoutDashboard;
  if (target.startsWith("plugin:")) return Plug;
  if (target.startsWith("channel:")) return AudioWaveform;
  if (target.startsWith("effect:")) return SlidersHorizontal;
  if (target.startsWith("guide:")) return BookOpen;
  if (target.startsWith("map:")) return MapIcon;
  if (target.startsWith("pick:")) return MessageCircleQuestion;
  return Gauge;
}
