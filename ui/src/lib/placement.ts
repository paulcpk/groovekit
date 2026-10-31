// Where pasted and duplicated things go, in the piano roll (notes on keys) and the playlist
// (clips in rows) alike: at their own times when nothing on their lanes is in the way, else
// right after the block they came from, and on after the copy made last when pasted again.

/** Something on a lane for a stretch of ticks: a note on its key, a clip in its row. */
export interface Span {
  lane: number;
  start: number;
  length: number;
}

const clash = (a: Span, b: Span) => a.lane === b.lane && a.start < b.start + b.length && b.start < a.start + a.length;

/** Whether any of the spans would overlap one of the others on its lane. */
export function blocked(spans: Span[], others: Span[]) {
  return spans.some((s) => others.some((o) => clash(s, o)));
}

const startOf = (spans: Span[]) => Math.min(...spans.map((s) => s.start));
const endOf = (spans: Span[]) => Math.max(...spans.map((s) => s.start + s.length));
const shifted = (spans: Span[], by: number) => spans.map((s) => ({ ...s, start: s.start + by }));

/** How far to move a copied block (in ticks) so it lands where nothing is in the way: not at
    all when its own times are free; else to the first grid line at or after its end (the piano
    roll's step, the playlist's bar), and on, a block at a time, past whatever is there. Given
    `after` (the copy pasted last), it goes on from there instead. */
export function placement(block: Span[], existing: Span[], grid: number, after?: Span[]): number {
  if (!block.length) return 0;
  if (!after && !blocked(block, existing)) return 0;
  const first = startOf(block);
  const step = Math.max(1, grid);
  let last = after ?? block;
  for (;;) {
    // At least a step on, so even a block of zero-length spans moves.
    const to = Math.max(Math.ceil(endOf(last) / step) * step, startOf(last) + step);
    const moved = shifted(block, to - first);
    if (!blocked(moved, existing)) return to - first;
    last = moved;
  }
}

const same = (a: Span[], b: Span[]) =>
  a.length === b.length && a.every((s, i) => s.lane === b[i].lane && s.start === b[i].start && s.length === b[i].length);

/** Remembers where the last paste went, so pasting the same block into the same target again
    goes on after that copy. Only while the copy is still where it was put (an undo or a move
    forgets it); copying something else starts over. */
export class PasteTrail {
  private last?: { target: string; block: Span[]; placed: Span[] };

  /** The shift for pasting `block` into `target` (by `placement`), remembered for the next paste. */
  place(target: string, block: Span[], existing: Span[], grid: number): number {
    const last = this.last;
    const still = last && last.target === target && same(last.block, block)
      && last.placed.every((p) => existing.some((e) => same([e], [p])));
    const shift = placement(block, existing, grid, still ? last.placed : undefined);
    this.last = { target, block, placed: shifted(block, shift) };
    return shift;
  }
}
