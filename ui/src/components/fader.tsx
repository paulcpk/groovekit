import type { AutomationTarget } from "@groovekit/schema/project";
import { useEffect, useRef, useState } from "react";
import { useRevealedAt } from "@/lib/reveal";
import { openControlMenu } from "./controlMenu";
import { startDrag } from "@/lib/drag";
import { formatDb, gainToDb } from "@/lib/model";

// Faders reach +24 dB (the engine's limit, for quiet hardware inputs), with most of the travel
// spent around unity.
const minDb = -70, maxDb = 24, curve = 1.6;

export function dbToFader(db: number) {
  if (!Number.isFinite(db) || db <= minDb) return 0;
  return Math.pow((Math.min(db, maxDb) - minDb) / (maxDb - minDb), curve);
}

export function faderToDb(pos: number) {
  if (pos <= 0.002) return -100;
  return Math.round((Math.pow(Math.min(1, pos), 1 / curve) * (maxDb - minDb) + minDb) * 10) / 10;
}

/** Vertical volume fader. Drag (Shift for fine), double-click or Alt-click for 0 dB; right-click
    to reset it or, with `automation`, to make an automation clip for what it sets. */
export function Fader({ db, onChange, onCommit, height = 140, automation }: {
  db: number; onChange?: (db: number) => void; onCommit: (db: number) => void; height?: number; automation?: AutomationTarget;
}) {
  const [live, setLive] = useState<number | null>(null);
  const shown = live ?? db;
  const pos = dbToFader(shown);
  const track = height - 12;
  const revealed = useRevealedAt(automation);
  const root = useRef<HTMLDivElement>(null);
  useEffect(() => {
    if (revealed !== undefined) root.current?.scrollIntoView({ block: "nearest", inline: "nearest" });
  }, [revealed]);

  return (
    <div
      ref={root}
      className="relative w-5 cursor-ns-resize"
      style={{ height }}
      title={`${formatDb(shown)} dB`}
      onDoubleClick={() => onCommit(0)}
      onContextMenu={(e) => openControlMenu(e, { automation, value: db, defaultValue: 0, format: (v) => `${formatDb(v)} dB`, onCommit })}
      onPointerDown={(e) => {
        e.stopPropagation();
        if (e.altKey) {
          onCommit(0);
          return;
        }
        const p0 = dbToFader(db);
        let value = db;
        startDrag(e, (_dx, dy, ev) => {
          // At least 180 px for the whole range, however short the fader is drawn.
          const p = Math.min(1, Math.max(0, p0 - dy / Math.max(track, 180) / (ev.shiftKey ? 8 : 1)));
          value = faderToDb(p);
          setLive(value);
          onChange?.(value);
        }, (_dx, _dy, _ev, moved) => {
          setLive(null);
          if (moved) onCommit(value);
        }, "ns-resize");
      }}
    >
      {revealed !== undefined && <span key={revealed} className="pointer-events-none absolute -inset-1 z-10 animate-agent-flash rounded-[4px] ring-2 ring-accent" />}
      <div className="absolute top-1.5 bottom-1.5 left-1/2 w-[3px] -translate-x-1/2 rounded bg-bg" />
      <div className="absolute left-0 right-0 h-px bg-faint" style={{ bottom: 6 + dbToFader(0) * track }} />
      <div
        className="absolute left-0 h-3 w-5 rounded-[2px] border border-[#55555d] bg-[#8a8a93] shadow"
        style={{ bottom: pos * track }}
      >
        <div className="mx-auto mt-[5px] h-px w-3 bg-[#2a2a2e]" />
      </div>
    </div>
  );
}

// Meters show -60..+6 dB.
function meterPos(peak: number) {
  const db = gainToDb(peak);
  if (!Number.isFinite(db)) return 0;
  return Math.min(1, Math.max(0, (db + 60) / 66));
}

const meterGradient = "linear-gradient(to top, #3fa66a 0%, #5fcf7d 72%, #e8c547 86%, #ef6a4f 92%, #ef4f4f 100%)";

/** Stereo peak meter. Levels arrive about 30 times a second; the bars ease down between them. */
export function Meter({ left, right, height = 140, width = 3 }: { left: number; right?: number; height?: number; width?: number }) {
  const bars = right === undefined ? [left] : [left, right];
  return (
    <div className="flex gap-px" style={{ height }}>
      {bars.map((v, i) => (
        <div key={i} className="relative overflow-hidden rounded-[1px] bg-bg" style={{ width }}>
          <div
            className="absolute bottom-0 left-0 right-0"
            style={{ height: `${meterPos(v) * 100}%`, background: meterGradient, backgroundSize: `100% ${height}px`, backgroundPosition: "bottom", transition: "height 90ms linear" }}
          />
        </div>
      ))}
    </div>
  );
}

/** A small activity light for channels. */
export function ActivityLed({ level }: { level: number }) {
  const on = meterPos(level);
  return <div className="h-2 w-2 rounded-full" style={{ background: on > 0.05 ? `rgba(111, 208, 140, ${0.35 + on * 0.65})` : "#2a2a2f" }} />;
}
