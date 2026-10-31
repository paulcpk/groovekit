import { X } from "lucide-react";
import { IconButton } from "@/components/controls";
import { startDrag } from "@/lib/drag";
import { closeWindow, focusWindow, moveWindow, type OpenWindow } from "@/state/windows";

/** A window floating over the app, moved by its title bar. */
/** onClose: what closing it by hand (its X, Escape) does, when that means more than closing it. */
export function FloatingWindow({ window: w, title, accent, extra, width, onClose, children }: {
  window: OpenWindow; title: React.ReactNode; accent?: string; extra?: React.ReactNode; width: number; onClose?: () => void; children: React.ReactNode;
}) {
  const close = onClose ?? (() => closeWindow(w.key));

  // Kept on screen, with at least the title bar reachable.
  const x = Math.min(Math.max(w.x, 8 - width + 120), innerWidth - 120);
  const y = Math.min(Math.max(w.y, 36), innerHeight - 60);

  return (
    <div
      className="pointer-events-auto absolute flex max-h-[calc(100vh-48px)] flex-col overflow-hidden rounded-lg border border-line bg-panel shadow-[0_18px_50px_rgb(0_0_0/0.55)]"
      style={{ left: x, top: y, width, zIndex: w.z }}
      onPointerDownCapture={() => focusWindow(w.key)}
      onKeyDown={(e) => e.key === "Escape" && close()}
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
        <IconButton size="sm" title="Close (Esc)" onClick={close}><X size={12} /></IconButton>
      </div>
      <div className="min-h-0 flex-1 overflow-y-auto">{children}</div>
    </div>
  );
}
