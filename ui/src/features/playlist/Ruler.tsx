import { setLoop, setPosition } from "@/features/shell/actions";
import { floorTo, snapTo, startDrag } from "@/lib/drag";
import { useUi } from "@/state/ui";
import { LOOP_STRIP_H, RULER_H, type Geometry } from "./geometry";

/** Bar numbers and the loop region. Top strip: drag a loop of two bars or more, or click to clear it.
    Below: click or drag to move the playhead. */
export function Ruler({ g, endTicks }: { g: Geometry; endTicks: number }) {
  const loop = useUi((s) => s.loop);
  const looping = useUi((s) => s.looping);
  const barPx = g.bar * g.pxPerTick;
  const every = Math.max(1, Math.pow(2, Math.ceil(Math.log2(44 / barPx))));
  const bars = Math.ceil(endTicks / g.bar);
  const labels = [];

  for (let b = 0; b < bars; b += every) {
    labels.push(
      <div key={b} className="absolute top-0 bottom-0 border-l border-line pl-1 text-2xs text-dim" style={{ left: b * barPx }}>
        <span className="absolute" style={{ top: LOOP_STRIP_H + 3 }}>{b + 1}</span>
      </div>,
    );
  }

  const tickAt = (e: { clientX: number }, el: HTMLElement) => Math.max(0, (e.clientX - el.getBoundingClientRect().left) / g.pxPerTick);

  return (
    <div
      className="relative flex-1 cursor-pointer border-b border-line bg-panel"
      style={{ height: RULER_H }}
      onPointerDown={(e) => {
        const el = e.currentTarget;
        const y = e.clientY - el.getBoundingClientRect().top;
        const t0 = tickAt(e, el);

        if (y < LOOP_STRIP_H) {
          const from = floorTo(t0, g.snap);
          let range = { start: from, end: from + g.snap };
          useUi.setState({ loop: range });
          startDrag(e, (_dx, _dy, ev) => {
            const t = snapTo(tickAt(ev, el), g.snap);
            range = t >= from ? { start: from, end: Math.max(from + g.snap, t) } : { start: t, end: from + g.snap };
            useUi.setState({ loop: range });
          }, () => {
            // A click or a short drag (under two bars) clears the loop instead of making a tiny one.
            if (range.end - range.start < 2 * g.bar) void setLoop(undefined, false);
            else void setLoop(range, true);
          }, "ew-resize");
          return;
        }

        void setPosition(floorTo(t0, g.snap));
        let pending: number | undefined;
        startDrag(e, (_dx, _dy, ev) => {
          const t = floorTo(tickAt(ev, el), g.snap);
          if (pending === undefined) requestAnimationFrame(() => {
            if (pending !== undefined) void setPosition(pending);
            pending = undefined;
          });
          pending = t;
        });
      }}
    >
      <div className="absolute inset-x-0 top-0 bg-bg/60" style={{ height: LOOP_STRIP_H }} />
      {loop && (
        <div
          className={looping ? "absolute top-0 rounded-[2px] bg-accent/80" : "absolute top-0 rounded-[2px] bg-faint/60"}
          style={{ left: loop.start * g.pxPerTick, width: (loop.end - loop.start) * g.pxPerTick, height: LOOP_STRIP_H - 2 }}
        />
      )}
      {labels}
    </div>
  );
}
