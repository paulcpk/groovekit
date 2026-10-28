import { useState } from "react";
import { Button, DragNumber } from "@/components/controls";
import { Dialog } from "@/components/dialog";
import { inDesktopApp } from "@/engine/client";
import { formatDb, songEnd, ticksPerBar } from "@/lib/model";
import { cn } from "@/lib/cn";
import { call, toast, useEngine } from "@/state/engine";
import { setUi, useUi } from "@/state/ui";

type Range = "song" | "loop" | "bars";
type Format = "wav" | "flac" | "mp3";

/** What the last export used, so the next one starts there. */
interface Settings {
  format: Format;
  sampleRate: number;
  bitDepth: number;
  bitrate: number;
  tail: number;
  stems: boolean;
  silenceBefore: boolean;
  silenceAfter: boolean;
  silenceBars: number;
  normalize: boolean;
  normalizeDb: number;
  dither: boolean;
}

const DEFAULTS: Settings = {
  format: "wav", sampleRate: 48000, bitDepth: 24, bitrate: 320, tail: 2, stems: false,
  silenceBefore: false, silenceAfter: false, silenceBars: 1, normalize: false, normalizeDb: -1, dither: true,
};

const SAVED = "groovekit.exportSettings";

function loadSettings(): Settings {
  try {
    return { ...DEFAULTS, ...JSON.parse(localStorage.getItem(SAVED) ?? "{}") };
  } catch {
    return DEFAULTS;
  }
}

const FORMATS: { value: Format; label: string; note: string }[] = [
  { value: "wav", label: "WAV", note: "Uncompressed: for mastering, sharing stems, burning to a CD." },
  { value: "flac", label: "FLAC", note: "Lossless and about half the size of a WAV." },
  { value: "mp3", label: "MP3", note: "Small, plays anywhere: for previews and sending around." },
];

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

function Check({ checked, onChange, disabled, children }: { checked: boolean; onChange: (v: boolean) => void; disabled?: boolean; children: React.ReactNode }) {
  return (
    <label className={cn("flex items-center gap-1.5", disabled && "opacity-40")}>
      <input type="checkbox" checked={checked} disabled={disabled} onChange={(e) => onChange(e.target.checked)} className="accent-[var(--color-accent)]" />
      {children}
    </label>
  );
}

/** Renders the song (or part of it) to WAV, FLAC or MP3, optionally with a stem per mixer insert,
    with silence around it and its peaks brought to a level if asked. */
export function ExportDialog() {
  const open = useUi((s) => s.exportOpen);
  const loop = useUi((s) => s.loop);
  const doc = useEngine((s) => s.doc);
  const project = useEngine((s) => s.project);
  const [range, setRange] = useState<Range>("song");
  const [fromBar, setFromBar] = useState(1);
  const [toBar, setToBar] = useState(9);
  const [settings, setSettings] = useState<Settings>(loadSettings);
  const [busy, setBusy] = useState(false);
  const bar = ticksPerBar(doc);
  const inserts = (doc?.mixer.inserts.length ?? 1) - 1;
  const row = "grid grid-cols-[100px_1fr] items-center gap-3 text-xs";
  const s = settings;

  const set = (changes: Partial<Settings>) => {
    const next = { ...settings, ...changes };

    // Each format's own limits.
    if (next.format === "mp3" && next.sampleRate > 48000) next.sampleRate = 48000;
    if (next.format === "flac" && next.bitDepth === 32) next.bitDepth = 24;

    setSettings(next);
    try {
      localStorage.setItem(SAVED, JSON.stringify(next));
    } catch {
      // Remembered for this time only.
    }
  };

  const barSeconds = doc ? (bar / 960) * (60 / doc.tempo) : 2;

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
      const label = FORMATS.find((f) => f.value === s.format)!.label;
      const picked = await save({ title: "Export", defaultPath: `${project ?? ""}/renders/${name}.${s.format}`, filters: [{ name: label, extensions: [s.format] }] });
      if (!picked) return;
      path = picked;
    }

    setBusy(true);
    toast(s.stems ? `Exporting the mix and ${inserts} stems…` : "Exporting…");

    try {
      const r = await call("render", {
        start, end, tailSeconds: s.tail, sampleRate: s.sampleRate, format: s.format, stems: s.stems,
        ...(s.format === "mp3" ? { bitrate: s.bitrate } : { bitDepth: s.bitDepth, dither: s.dither }),
        ...(s.silenceBefore ? { silenceBefore: s.silenceBars * barSeconds } : {}),
        ...(s.silenceAfter ? { silenceAfter: s.silenceBars * barSeconds } : {}),
        ...(s.normalize ? { normalize: s.normalizeDb } : {}),
        ...(path ? { path } : {}),
      });
      toast(`Exported ${r.path.split("/").pop()}${r.stems ? ` and ${r.stems.length} stems` : ""} · ${r.analysis.integratedLufs.toFixed(1)} LUFS, true peak ${formatDb(r.analysis.truePeakDb)} dB`);
      setUi({ exportOpen: false });
    } finally {
      setBusy(false);
    }
  };

  const rates = s.format === "mp3" ? [44100, 48000] : [44100, 48000, 88200, 96000];

  return (
    <Dialog open={open} onOpenChange={(o) => setUi({ exportOpen: o })} title="Export" className="w-[500px]">
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
          <span className="text-dim">Format</span>
          <div className="flex flex-col gap-1">
            <Choice<Format> value={s.format} onChange={(format) => set({ format })} options={FORMATS} />
            <span className="text-2xs text-faint">{FORMATS.find((f) => f.value === s.format)!.note}</span>
          </div>
        </div>
        {s.format === "mp3" ? (
          <div className={row}>
            <span className="text-dim">Bit rate</span>
            <Choice value={s.bitrate} onChange={(bitrate) => set({ bitrate })} options={[128, 192, 256, 320].map((b) => ({ value: b, label: `${b} kbit/s` }))} />
          </div>
        ) : (
          <div className={row}>
            <span className="text-dim">Bit depth</span>
            <Choice value={s.bitDepth} onChange={(bitDepth) => set({ bitDepth })}
              options={(s.format === "flac" ? [16, 24] : [16, 24, 32]).map((b) => ({ value: b, label: b === 32 ? "32-bit float" : `${b}-bit` }))} />
          </div>
        )}
        <div className={row}>
          <span className="text-dim">Sample rate</span>
          <Choice value={s.sampleRate} onChange={(sampleRate) => set({ sampleRate })} options={rates.map((r) => ({ value: r, label: `${r / 1000} kHz` }))} />
        </div>
        {s.format !== "mp3" && s.bitDepth === 16 && (
          <div className={row}>
            <span className="text-dim">Dither</span>
            <Check checked={s.dither} onChange={(dither) => set({ dither })}>
              <span className="text-faint">a little noise, so quiet fades don't turn grainy at 16 bits</span>
            </Check>
          </div>
        )}

        <div className={row}>
          <span className="text-dim">Silence</span>
          <span className="flex flex-wrap items-center gap-x-3 gap-y-1">
            <Check checked={s.silenceBefore} onChange={(silenceBefore) => set({ silenceBefore })}>before</Check>
            <Check checked={s.silenceAfter} onChange={(silenceAfter) => set({ silenceAfter })}>after</Check>
            <span className={cn("flex items-center gap-1 text-faint", !s.silenceBefore && !s.silenceAfter && "opacity-40")}>
              <DragNumber value={s.silenceBars} min={0.25} max={16} step={0.25} format={(v) => `${v} bar${v === 1 ? "" : "s"}`}
                onCommit={(silenceBars) => set({ silenceBars })} className="text-fg" />
              ({(s.silenceBars * barSeconds).toFixed(1)} s)
            </span>
          </span>
        </div>
        <div className={row}>
          <span className="text-dim">Tail</span>
          <DragNumber value={s.tail} min={0} max={30} step={0.5} format={(v) => `${v.toFixed(1)} s after the end, for reverbs and delays`} onCommit={(tail) => set({ tail })} className="text-fg" />
        </div>
        <div className={row}>
          <span className="text-dim">Normalise</span>
          <span className="flex items-center gap-1.5">
            <Check checked={s.normalize} onChange={(normalize) => set({ normalize })}>peaks to</Check>
            <span className={cn(!s.normalize && "opacity-40")}>
              <DragNumber value={s.normalizeDb} min={-24} max={0} step={0.1} format={(v) => `${v.toFixed(1)} dB`} onCommit={(normalizeDb) => set({ normalizeDb })} className="text-fg" />
            </span>
            <span className="text-faint">(stems by the same amount)</span>
          </span>
        </div>
        <div className={row}>
          <span className="text-dim">Stems</span>
          <Check checked={s.stems} disabled={inserts === 0} onChange={(stems) => set({ stems })}>
            <span className="text-faint">{inserts === 0 ? "route channels to mixer inserts to export stems" : `also one file per mixer insert (${inserts}), next to the mix`}</span>
          </Check>
        </div>
        {!inDesktopApp && <div className="text-2xs text-faint">Files go to the project's renders folder.</div>}
        <div className="flex justify-end gap-2">
          <Button size="sm" onClick={() => set(DEFAULTS)}>Defaults</Button>
          <div className="flex-1" />
          <Button size="sm" onClick={() => setUi({ exportOpen: false })}>Cancel</Button>
          <Button size="sm" active tone="accent" disabled={busy} onClick={() => void run()}>{busy ? "Exporting…" : "Export"}</Button>
        </div>
      </div>
    </Dialog>
  );
}
