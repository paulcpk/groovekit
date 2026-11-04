import { X } from "lucide-react";
import { IconButton } from "@/components/controls";
import { startDrag } from "@/lib/drag";
import { useEffect, useRef } from "react";
import { closeWindow, focusWindow, hideWindow, moveWindow, resizeWindow, useWindows, type OpenWindow } from "@/state/windows";
import { closeOverview, flyTo, overviewMs, overviewOpacity, useOverview } from "@/features/windows/overviewState";
import { cn } from "@/lib/cn";

/** A window floating over the app, moved by its title bar. Escape sends it to the background
    (still open, as clicking the app's own window does); its X closes it.
    name: what the window switcher calls it.
    onClose: what closing it by hand (its X) does, when that means more than closing it.
    resizable: its top and bottom edges drag to make it taller or shorter, as on the Mac. */
export function FloatingWindow({ window: w, title, name, accent, extra, width, onClose, resizable, children }: {
  window: OpenWindow; title: React.ReactNode; name: string; accent?: string; extra?: React.ReactNode; width: number; onClose?: () => void; resizable?: boolean;
  children: React.ReactNode;
}) {
  const close = onClose ?? (() => closeWindow(w.key));
  const box = useRef<HTMLDivElement>(null);
  // In the overview of the open windows (Cmd+Up): moved to its place there, live, scaled down.
  const placed = useOverview((s) => (s.phase === "open" ? s.items.find((i) => i.key === w.key) : undefined));
  const inOverview = useOverview((s) => s.phase !== "closed");
  const selected = useOverview((s) => s.selected === w.key);
  const opacity = useOverview((s) => {
    const item = s.items.find((i) => i.key === w.key);
    return item ? overviewOpacity(s.phase, item, s.picked, s.phase === "closing" && !s.picked) : 1;
  });
  // In the background it stays (keeping its place, what it's scrolled to), unseen.
  const shown = !w.hidden || inOverview;

  useEffect(() => {
    if (useWindows.getState().names[w.key] !== name) useWindows.setState((s) => ({ names: { ...s.names, [w.key]: name } }));
  }, [w.key, name]);

  // Kept on screen, with at least the title bar reachable.
  const x = Math.min(Math.max(w.x, 8 - width + 120), innerWidth - 120);
  const y = Math.min(Math.max(w.y, 36), innerHeight - 60);

  // Dragging an edge: the bottom one keeps the top where it is, the top one the bottom.
  const edge = (e: React.PointerEvent, which: "top" | "bottom") => {
    if (e.button !== 0 || !box.current) return;
    e.preventDefault();
    const startHeight = box.current.getBoundingClientRect().height;
    const minimum = 140, maximum = innerHeight - 48;
    startDrag(e, (_, dy) => {
      if (which === "bottom") return resizeWindow(w.key, y, Math.min(Math.max(startHeight + dy, minimum), maximum - (y - 36)));
      const height = Math.min(Math.max(startHeight - dy, minimum), maximum);
      resizeWindow(w.key, Math.max(36, y + startHeight - height), height);
    }, undefined, "ns-resize");
  };

  return (
    <div
      ref={box}
      data-window-key={w.key}
      className={cn("pointer-events-auto absolute flex max-h-[calc(100vh-48px)] origin-top-left flex-col overflow-hidden rounded-lg border bg-panel shadow-[0_18px_50px_rgb(0_0_0/0.55)]",
        placed && selected ? "border-accent" : "border-line")}
      style={{ left: x, top: y, width, zIndex: w.z, height: resizable ? w.height : undefined,
        visibility: shown ? undefined : "hidden", opacity: inOverview ? opacity : undefined,
        transform: placed ? flyTo(placed.from, placed.to) : undefined,
        transition: `transform ${overviewMs}ms cubic-bezier(0.2, 0.8, 0.2, 1), opacity ${overviewMs}ms cubic-bezier(0.2, 0.8, 0.2, 1)` }}
      aria-hidden={!shown || undefined}
      onPointerDownCapture={() => !inOverview && focusWindow(w.key)}
      onKeyDown={(e) => e.key === "Escape" && hideWindow(w.key)}
    >
      <div
        className="flex h-8 shrink-0 cursor-grab items-center gap-2 border-b border-line bg-raised px-2 select-none active:cursor-grabbing"
        style={accent ? { boxShadow: `inset 0 2px 0 ${accent}` } : undefined}
        onPointerDown={(e) => {
          if (e.button !== 0 || (e.target as HTMLElement).closest("button, input, [data-no-drag]")) return;
          startDrag(e, (dx, dy) => moveWindow(w.key, x + dx, y + dy));
        }}
      >
        <div className="flex min-w-0 flex-1 items-center gap-2 text-sm font-medium">{title}</div>
        {extra}
        <IconButton size="sm" title="Close (Esc sends it to the background)" onClick={close}><X size={12} /></IconButton>
      </div>
      <div className="min-h-0 flex-1 overflow-y-auto">{children}</div>
      {inOverview && (
        // The window's controls rest while it's in the overview: a click anywhere picks it.
        <div className="absolute inset-0 z-10 cursor-pointer" onClick={() => closeOverview(w.key)}
          onPointerEnter={() => useOverview.setState({ selected: w.key })} />
      )}
      {resizable && (
        <>
          <div className="absolute inset-x-0 top-0 h-1.5 cursor-ns-resize" onPointerDown={(e) => edge(e, "top")} title="Drag to resize" />
          <div className="absolute inset-x-0 bottom-0 h-1.5 cursor-ns-resize" onPointerDown={(e) => edge(e, "bottom")} title="Drag to resize" />
        </>
      )}
    </div>
  );
}
