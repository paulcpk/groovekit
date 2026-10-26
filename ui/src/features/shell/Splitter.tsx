import { cn } from "@/lib/cn";
import { startDrag } from "@/lib/drag";

/** A thin handle between panes; reports how far it was dragged since the last call. */
export function Splitter({ direction, onDrag }: { direction: "x" | "y"; onDrag: (delta: number) => void }) {
  return (
    <div
      className={cn(
        "relative z-10 shrink-0 bg-line transition-colors hover:bg-accent/60",
        direction === "x" ? "w-px cursor-col-resize after:absolute after:inset-y-0 after:-left-1 after:-right-1" : "h-px cursor-row-resize after:absolute after:inset-x-0 after:-top-1 after:-bottom-1",
      )}
      onPointerDown={(e) => {
        let last = 0;
        startDrag(e, (dx, dy) => {
          const d = direction === "x" ? dx : dy;
          onDrag(d - last);
          last = d;
        }, undefined, direction === "x" ? "col-resize" : "row-resize");
      }}
    />
  );
}
