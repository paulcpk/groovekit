import { describe, expect, it } from "vitest";
import { blocked, PasteTrail, placement, type Span } from "../src/lib/placement.ts";

const BAR = 3840, STEP = 240;
const at = (by: number, block: Span[]) => block.map((s) => ({ ...s, start: s.start + by }));

describe("where copies go", () => {
  // A kick on the first two beats and a snare on the second (keys 36 and 38).
  const block: Span[] = [{ lane: 36, start: 0, length: 240 }, { lane: 36, start: 960, length: 240 }, { lane: 38, start: 960, length: 240 }];

  it("keeps the block's own times where nothing on its lanes is in the way", () => {
    expect(placement(block, [], STEP)).toBe(0);
    // Hats on another key, and a kick between the block's, don't get in the way.
    expect(placement(block, [{ lane: 42, start: 0, length: 3840 }, { lane: 36, start: 480, length: 240 }], STEP)).toBe(0);
  });

  it("goes right after the block when its own spot is taken", () => {
    // The block ends at 1200; the next 16th is there already.
    expect(placement(block, block, STEP)).toBe(1200);
    // On a 1/4 grid, the next beat; with no grid, right at the end.
    expect(placement(block, block, 960)).toBe(1920);
    expect(placement(block, block, 1)).toBe(1200);
  });

  it("moves clips to the next bar line, the notes' block to the next step", () => {
    const clips: Span[] = [{ lane: 0, start: 0, length: 1.5 * BAR }, { lane: 2, start: BAR, length: BAR }];
    expect(placement(clips, clips, BAR)).toBe(2 * BAR);
    const notes: Span[] = [{ lane: 60, start: 0, length: 1.5 * BAR }];
    expect(placement(notes, notes, STEP)).toBe(1.5 * BAR);
  });

  it("goes on past whatever is after the block, a block at a time", () => {
    const clips: Span[] = [{ lane: 1, start: 0, length: BAR }];
    const existing = [...clips, { lane: 1, start: BAR, length: 2 * BAR }];
    expect(placement(clips, existing, BAR)).toBe(3 * BAR);
    // Something in another row is no obstacle.
    expect(placement(clips, [...clips, { lane: 0, start: BAR, length: BAR }], BAR)).toBe(BAR);
  });

  it("appends repeated pastes, each after the last", () => {
    const trail = new PasteTrail();
    const clips: Span[] = [{ lane: 0, start: 0, length: BAR }, { lane: 1, start: 0, length: 2 * BAR }];
    let song = [...clips];
    const paste = () => {
      const shift = trail.place("playlist", clips, song, BAR);
      song = [...song, ...at(shift, clips)];
      return shift;
    };
    expect(paste()).toBe(2 * BAR);
    expect(paste()).toBe(4 * BAR);
    expect(paste()).toBe(6 * BAR);
    expect(blocked(song.slice(-2), song.slice(0, -2))).toBe(false);
  });

  it("goes on after the last paste even where an earlier spot is free again", () => {
    const trail = new PasteTrail();
    const notes: Span[] = [{ lane: 60, start: 0, length: 960 }];
    const first = trail.place("p/c", notes, notes, STEP);
    expect(first).toBe(960);
    // The first copy stays; the original is deleted: still after the copy, not back at 0.
    expect(trail.place("p/c", notes, at(first, notes), STEP)).toBe(1920);
  });

  it("forgets the last paste once that copy is gone (undone or moved)", () => {
    const trail = new PasteTrail();
    const notes: Span[] = [{ lane: 60, start: 0, length: 960 }];
    expect(trail.place("p/c", notes, notes, STEP)).toBe(960);
    // Undone: the copy isn't there, so it goes where it went before.
    expect(trail.place("p/c", notes, notes, STEP)).toBe(960);
  });

  it("keeps the original times in another target where nothing is in the way", () => {
    const trail = new PasteTrail();
    const notes: Span[] = [{ lane: 36, start: 480, length: 240 }];
    expect(trail.place("a/kick", notes, notes, STEP)).toBe(240);
    expect(trail.place("b/kick", notes, [], STEP)).toBe(0);
    // Then again in that target: after that copy.
    expect(trail.place("b/kick", notes, notes, STEP)).toBe(240);
  });

  it("starts over when something else is copied", () => {
    const trail = new PasteTrail();
    const a: Span[] = [{ lane: 0, start: 0, length: BAR }];
    const b: Span[] = [{ lane: 0, start: 4 * BAR, length: BAR }];
    expect(trail.place("playlist", a, [], BAR)).toBe(0);
    // b's own times are free: it isn't put after a's paste.
    expect(trail.place("playlist", b, a, BAR)).toBe(0);
  });
});
