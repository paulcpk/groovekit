import { useState } from "react";
import { Button, DragNumber } from "@/components/controls";
import { Dialog } from "@/components/dialog";
import { inDesktopApp } from "@/engine/client";
import { formatDb, songEnd, ticksPerBar } from "@/lib/model";
import { cn } from "@/lib/cn";
import { call, toast, useEngine } from "@/state/engine";
import { setUi, useUi } from "@/state/ui";

type Range = "song" | "loop" | "bars";

function Choice<T extends string | number>({ value, options, onChange }: { value: T; options: { value: T; label: string; disabled?: boolean }[]; onChange: (v: T) => void }) {
  return (
    <div className="flex flex-wrap gap-1">
      {options.map((o) => (
        <button key={String(o.value)} type="button" disabled={o.disabled} onClick={() => onChange(o.value)}
          className={cn("h-6 rounded-[4px] px-2 text-xs disabled:opacity-40", value === o.value ? "bg-accent text-black" : "bg-control text-dim hover:text-fg")}>
          {o.label}
        </button>
      ))}
    </div>
  );
}

/** Renders the song (or part of it) to WAV, optionally with a stem per mixer insert. */
export function ExportDialog() {
  const open = useUi((s) => s.exportOpen);
  const loop = useUi((s) => s.loop);
  const doc = useEngine((s) => s.doc);
  const project = useEngine((s) => s.project);
  const [range, setRange] = useState<Range>("song");
  const [fromBar, setFromBar] = useState(1);
  const [toBar, setToBar] = useState(9);
  const [sampleRate, setSampleRate] = useState(48000);
  const [bitDepth, setBitDepth] = useState(24);
  const [tail, setTail] = useState(2);
  const [stems, setStems] = useState(false);
  const [busy, setBusy] = useState(false);
  const bar = ticksPerBar(doc);
  const inserts = (doc?.mixer.inserts.length ?? 1) - 1;
  const row = "grid grid-cols-[100px_1fr] items-center gap-3 text-xs";

  const run = async () => {
    if (!doc) return;
    const [start, end] = range === "loop" && loop ? [loop.start, loop.end] : range === "bars" ? [(fromBar - 1) * bar, (toBar - 1) * bar] : [0, songEnd(doc)];

    if (end <= start) {
      toast("The range is empty", "error");
      return;
    }

    let path: string | undefined;

    if (inDesktopApp) {
      const { save } = await import("@tauri-apps/plugin-dialog");
      const name = (doc.name || project?.split("/").pop() || "Song").replace(/[/:]/g, "-");
      const picked = await save({ title: "Export", defaultPath: `${project ?? ""}/renders/${name}.wav`, filters: [{ name: "WAV", extensions: ["wav"] }] });
      if (!picked) return;
      path = picked;
    }

    setBusy(true);
    toast(stems ? `Exporting the mix and ${inserts} stems…` : "Exporting…");

    try {
      const r = await call("render", { start, end, tailSeconds: tail, sampleRate, bitDepth, stems, ...(path ? { path } : {}) });
      toast(`Exported ${r.path.split("/").pop()}${r.stems ? ` and ${r.stems.length} stems` : ""} · ${r.analysis.integratedLufs.toFixed(1)} LUFS, true peak ${formatDb(r.analysis.truePeakDb)} dB`);
      setUi({ exportOpen: false });
    } finally {
      setBusy(false);
    }
  };

  return (
    <Dialog open={open} onOpenChange={(o) => setUi({ exportOpen: o })} title="Export" className="w-[460px]">
      <div className="flex flex-col gap-3">
        <div className={row}>
          <span className="text-dim">Range</span>
          <Choice<Range> value={range} onChange={setRange} options={[
            { value: "song", label: "Whole song" },
            { value: "loop", label: "Loop region", disabled: !loop },
            { value: "bars", label: "Bars" },
          ]} />
        </div>
        {range === "bars" && (
          <div className={row}>
            <span className="text-dim">From bar</span>
            <span className="flex items-center gap-2">
              <DragNumber value={fromBar} min={1} max={999} onCommit={(v) => setFromBar(Math.round(v))} className="text-fg" />
              <span className="text-faint">to the start of bar</span>
              <DragNumber value={toBar} min={2} max={1000} onCommit={(v) => setToBar(Math.round(v))} className="text-fg" />
            </span>
          </div>
        )}
        <div className={row}>
          <span className="text-dim">Sample rate</span>
          <Choice value={sampleRate} onChange={setSampleRate} options={[44100, 48000, 96000].map((r) => ({ value: r, label: `${r / 1000} kHz` }))} />
        </div>
        <div className={row}>
          <span className="text-dim">Bit depth</span>
          <Choice value={bitDepth} onChange={setBitDepth} options={[16, 24, 32].map((b) => ({ value: b, label: b === 32 ? "32-bit float" : `${b}-bit` }))} />
        </div>
        <div className={row}>
          <span className="text-dim">Tail</span>
          <DragNumber value={tail} min={0} max={30} step={0.5} format={(v) => `${v.toFixed(1)} s after the end`} onCommit={setTail} className="text-fg" />
        </div>
        <label className={row}>
          <span className="text-dim">Stems</span>
          <span className="flex items-center gap-2">
            <input type="checkbox" checked={stems} disabled={inserts === 0} onChange={(e) => setStems(e.target.checked)} className="accent-[var(--color-accent)]" />
            <span className="text-faint">{inserts === 0 ? "route channels to mixer inserts to export stems" : `also one file per mixer insert (${inserts}), next to the mix`}</span>
          </span>
        </label>
        {!inDesktopApp && <div className="text-2xs text-faint">Files go to the project's renders folder.</div>}
        <div className="flex justify-end gap-2">
          <Button size="sm" onClick={() => setUi({ exportOpen: false })}>Cancel</Button>
          <Button size="sm" active tone="accent" disabled={busy} onClick={() => void run()}>{busy ? "Exporting…" : "Export"}</Button>
        </div>
      </div>
    </Dialog>
  );
}
