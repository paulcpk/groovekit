/** Follows the pointer from a pointerdown until release. onMove gets the distance moved so far. */
export function startDrag(
  e: React.PointerEvent | PointerEvent,
  onMove: (dx: number, dy: number, event: PointerEvent) => void,
  onEnd?: (dx: number, dy: number, event: PointerEvent, moved: boolean) => void,
  cursor?: string,
) {
  const x0 = e.clientX;
  const y0 = e.clientY;
  let moved = false;
  const previousCursor = document.body.style.cursor;
  if (cursor) document.body.style.cursor = cursor;

  const move = (ev: PointerEvent) => {
    const dx = ev.clientX - x0;
    const dy = ev.clientY - y0;
    if (!moved && Math.abs(dx) + Math.abs(dy) < 3) return;
    moved = true;
    onMove(dx, dy, ev);
  };

  const up = (ev: PointerEvent) => {
    window.removeEventListener("pointermove", move);
    window.removeEventListener("pointerup", up);
    window.removeEventListener("pointercancel", up);
    document.body.style.cursor = previousCursor;
    onEnd?.(ev.clientX - x0, ev.clientY - y0, ev, moved);
  };

  window.addEventListener("pointermove", move);
  window.addEventListener("pointerup", up);
  window.addEventListener("pointercancel", up);
}

export function snapTo(value: number, step: number) {
  return step > 0 ? Math.round(value / step) * step : Math.round(value);
}

export function floorTo(value: number, step: number) {
  return step > 0 ? Math.floor(value / step) * step : Math.round(value);
}
