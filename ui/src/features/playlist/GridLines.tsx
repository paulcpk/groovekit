import { memo } from "react";
import { labelEvery, type Geometry } from "./geometry";

/** The lanes' grid, drawn once behind all of them: the strongest lines on the bars the ruler
    numbers, then the other bars and the beats as they get room. Lines sit on whole pixels, so
    they don't shimmer at fractional zooms the way a repeating background does. */
export const GridLines = memo(function GridLines({ g, width, height, left, top }: {
  g: Geometry; width: number; height: number; left: number; top: number;
}) {
  const barPx = g.bar * g.pxPerTick;
  const beatPx = g.beat * g.pxPerTick;
  const every = labelEvery(barPx);
  const beatsPerBar = Math.round(g.bar / g.beat);
  const major: string[] = [], bars: string[] = [], beats: string[] = [];
  const line = (x: number) => `M${Math.round(x) + 0.5} 0V${height}`;

  for (let b = 0; b * barPx <= width; b++) {
    if (b % every === 0) major.push(line(b * barPx));
    else if (barPx >= 8) bars.push(line(b * barPx));

    if (beatPx >= 8)
      for (let k = 1; k < beatsPerBar; k++) beats.push(line(b * barPx + k * beatPx));
  }

  return (
    <svg className="pointer-events-none absolute" style={{ left, top }} width={width} height={height} shapeRendering="crispEdges">
      <path d={beats.join("")} stroke="var(--color-grid)" />
      <path d={bars.join("")} stroke="var(--color-grid-strong)" />
      <path d={major.join("")} stroke="var(--color-grid-major)" />
    </svg>
  );
});
