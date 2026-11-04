import { Bot, FolderOpen, Layers, ListMusic, Piano, SlidersVertical } from "lucide-react";
import { cn } from "@/lib/cn";
import { useEngine } from "@/state/engine";
import { setUi, type BottomPanel, type SidebarTab } from "@/state/ui";
import { closeOverview, flyTo, moveSelection, overviewMs, toggleOverview, useOverview, type OverviewItem } from "./overviewState";

const ease = `${overviewMs}ms cubic-bezier(0.2, 0.8, 0.2, 1)`;

/** The overview's backdrop, the plugins' pictures and the views along the top. GrooveKit's own
    windows move themselves (FloatingWindow). Sits in the windows' layer, under them. */
export function Overview() {
  const phase = useOverview((s) => s.phase);
  const items = useOverview((s) => s.items);
  const shown = phase === "open";

  if (phase === "closed") return null;

  return (
    <>
      <div
        className="pointer-events-auto absolute inset-0 bg-black/60 backdrop-blur-[6px]"
        style={{ zIndex: 0, opacity: shown ? 1 : 0, transition: `opacity ${ease}` }}
        onClick={() => closeOverview()}
      />
      <Views shown={shown} empty={!items.length} />
      {items.filter((i) => i.plugin).map((item) => <PluginPicture key={item.key} item={item} shown={shown} />)}
    </>
  );
}

/** A plugin's window as the engine pictured it, flying from where the window was to its place. */
function PluginPicture({ item, shown }: { item: OverviewItem; shown: boolean }) {
  const selected = useOverview((s) => s.selected === item.key);
  const label = usePluginLabel(item.key);
  const { to, from } = item;

  return (
    <button
      type="button"
      className="group pointer-events-auto absolute origin-top-left text-left outline-none"
      style={{ left: to.x, top: to.y, width: to.width, height: to.height, zIndex: 1,
        transform: shown ? "none" : flyTo(to, from), transition: `transform ${ease}` }}
      onClick={() => closeOverview(item.key)}
      onPointerEnter={() => useOverview.setState({ selected: item.key })}
      title={label}
    >
      {item.picture
        ? <img src={item.picture} alt="" draggable={false} className={cn("h-full w-full rounded-md shadow-[0_18px_50px_rgb(0_0_0/0.55)] ring-2 transition-shadow", selected && shown ? "ring-accent" : "ring-transparent")} />
        : <div className={cn("flex h-full w-full items-center justify-center rounded-md bg-panel text-sm text-dim ring-2", selected ? "ring-accent" : "ring-line")}>{label}</div>}
      <Label text={label} visible={shown} />
    </button>
  );
}

/** The name under a window in the overview. */
export function Label({ text, visible }: { text: string; visible: boolean }) {
  return (
    <div className="pointer-events-none absolute inset-x-0 top-full mt-2 flex justify-center transition-opacity" style={{ opacity: visible ? 1 : 0 }}>
      <span className="max-w-full truncate rounded-full bg-black/70 px-2.5 py-0.5 text-xs text-fg">{text}</span>
    </div>
  );
}

/** What a plugin's window belongs to: the channel it plays, or the insert and effect slot. */
function usePluginLabel(key: string) {
  return useEngine((s) => {
    const doc = s.doc;
    if (key.startsWith("channel:")) {
      const channel = doc?.channels.find((c) => c.id === key.slice(8));
      const plugin = channel?.instrument.type === "plugin" ? channel.instrument.plugin.name : undefined;
      return [channel?.name ?? key.slice(8), plugin].filter(Boolean).join(" · ");
    }
    const [insertId, effectId] = key.slice(7).split("/");
    const insert = doc?.mixer.inserts.find((i) => i.id === insertId);
    const effect = insert?.effects?.find((e) => e.id === effectId);
    const plugin = effect?.type === "plugin" ? effect.plugin.name : effectId;
    return `${insert?.name ?? insertId} · ${plugin}`;
  });
}

const views: { label: string; icon: React.ReactNode; bottom?: BottomPanel | null; sidebar?: SidebarTab }[] = [
  { label: "Playlist", icon: <ListMusic size={14} />, bottom: null },
  { label: "Channel rack", icon: <Layers size={14} />, bottom: "rack" },
  { label: "Piano roll", icon: <Piano size={14} />, bottom: "piano" },
  { label: "Mixer", icon: <SlidersVertical size={14} />, bottom: "mixer" },
  { label: "Browser", icon: <FolderOpen size={14} />, sidebar: "browser" },
  { label: "Agent", icon: <Bot size={14} />, sidebar: "activity" },
];

/** The app's views along the top, like the Spaces in the Mac's overview: one click opens one. */
function Views({ shown, empty }: { shown: boolean; empty: boolean }) {
  return (
    <div className="pointer-events-none absolute inset-x-0 top-0 flex flex-col items-center gap-2 pt-10"
      style={{ zIndex: 1, opacity: shown ? 1 : 0, transform: shown ? "none" : "translateY(-12px)", transition: `opacity ${ease}, transform ${ease}` }}>
      <div className="pointer-events-auto flex items-center gap-1 rounded-lg border border-line bg-panel/90 p-1 shadow-lg">
        {views.map((v) => (
          <button key={v.label} type="button"
            className="flex h-7 items-center gap-1.5 rounded-[4px] px-2.5 text-xs text-dim hover:bg-hover hover:text-fg"
            onClick={() => {
              if (v.bottom === null) setUi({ bottomOpen: false });
              else if (v.bottom) setUi({ bottomOpen: true, bottomPanel: v.bottom });
              if (v.sidebar) setUi({ sidebarOpen: true, sidebarTab: v.sidebar });
              closeOverview();
            }}>
            {v.icon}{v.label}
          </button>
        ))}
      </div>
      {empty && <div className="mt-[30vh] text-sm text-dim">No windows open</div>}
    </div>
  );
}

/** The overview's keys, heard before anything else (registered first, at the capture): Cmd+Up
    (or F3, the Mac's own key for it) opens and closes it; while it's up, Escape goes back, the
    arrows choose a window and Enter brings it forward. True when the key was the overview's. */
export function overviewKey(e: KeyboardEvent): boolean {
  const open = useOverview.getState().phase !== "closed";
  const arrows: Record<string, [number, number]> = { ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowUp: [0, -1], ArrowDown: [0, 1] };

  if ((e.metaKey && !e.shiftKey && !e.altKey && e.key === "ArrowUp") || e.key === "F3") {
    if (!e.repeat) toggleOverview();
  } else if (!open) {
    return false;
  } else if (e.key === "Escape") closeOverview();
  else if (e.key === "Enter") closeOverview(useOverview.getState().selected);
  else if (arrows[e.key]) moveSelection(...arrows[e.key]);
  else return false;

  e.preventDefault();
  e.stopImmediatePropagation();
  return true;
}
