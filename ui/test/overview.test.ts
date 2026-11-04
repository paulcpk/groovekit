import { describe, expect, it } from "vitest";
import { arrange } from "../src/features/windows/overviewState";

const area = { x: 40, y: 92, width: 1360, height: 770 };
const inside = (r: { x: number; y: number; width: number; height: number }) =>
  r.x >= area.x - 0.01 && r.y >= area.y - 0.01 && r.x + r.width <= area.x + area.width + 0.01 && r.y + r.height <= area.y + area.height + 0.01;
const overlap = (a: { x: number; y: number; width: number; height: number }, b: typeof a) =>
  a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;

describe("the overview's layout", () => {
  it("never makes a window bigger than it is", () => {
    const [only] = arrange([{ width: 400, height: 300 }], area);
    expect(only.width).toBe(400);
    expect(only.height).toBe(300);
    expect(inside(only)).toBe(true);
  });

  it("fits many windows in the area, side by side, keeping their shapes", () => {
    const sizes = [
      { width: 900, height: 620 }, { width: 760, height: 520 }, { width: 520, height: 700 },
      { width: 1100, height: 680 }, { width: 640, height: 420 }, { width: 380, height: 520 },
    ];
    const slots = arrange(sizes, area);
    expect(slots).toHaveLength(sizes.length);
    slots.forEach((s, i) => {
      expect(inside(s)).toBe(true);
      expect(s.width / s.height).toBeCloseTo(sizes[i].width / sizes[i].height, 5);
      slots.slice(i + 1).forEach((t) => expect(overlap(s, t)).toBe(false));
    });
    // Using rows, not one long line: each shown at a useful size.
    expect(Math.min(...slots.map((s, i) => s.width / sizes[i].width))).toBeGreaterThan(0.3);
  });

  it("has nothing to place without windows", () => {
    expect(arrange([], area)).toEqual([]);
  });
});
