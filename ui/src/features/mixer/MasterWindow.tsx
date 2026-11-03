import { ChevronDown, Loader2, Play, Square, Wand2 } from "lucide-react";
import { useEffect, useMemo, useRef, useState } from "react";
import { Button } from "@/components/controls";
import { Dropdown, DropdownContent, DropdownItem, DropdownLabel, DropdownSeparator, DropdownSub, DropdownTrigger } from "@/components/menu";
import { FloatingWindow } from "@/components/window";
import { refreshSongs, useMaps } from "@/features/agent/maps";
import { stopPreview, useStopPreviewsOnClose } from "@/features/shell/actions";
import { cn } from "@/lib/cn";
import { pickFile } from "@/lib/files";
import { call, useEngine } from "@/state/engine";
import type { OpenWindow } from "@/state/windows";

/** What master.match measures of a master. */
interface Profile {
  lufs: number;
  truePeak: number;
  peakToLoudness: number;
  tone: number[];
  punch: number[];
  width: number[];
  monoBelowHz: number;
}

interface Match {
  references: string[];
  reference: Profile;
  before: Profile;
  after: Profile;
  targetLufs: number;
  close: boolean;
  remaining: string[];
  renders: number;
  files: { before: string; after: string; reference: string };
  filesLufs: number;
  balance?: { part: string; song: number; reference: number; advice?: string }[];
  loudnessNote?: string;
}

/** What to master like: a song map, a style's average, or a stretch of a file. */
type Reference =
  | { kind: "map"; id: string; title: string }
  | { kind: "style"; style: string }
  | { kind: "file"; file: string; from: number };

const BANDS = ["Lows", "Low mids", "High mids", "Highs"];
const LOUDNESS: [number | undefined, string][] = [[undefined, "As loud as the reference"], [-14, "-14 LUFS (streaming)"], [-11, "-11 LUFS"], [-9, "-9 LUFS"], [-7, "-7 LUFS (club)"]];

// The last match stays while the app runs, so closing the window doesn't lose it.
let last: { reference: Reference; result: Match } | undefined;

const label = (r: Reference) => r.kind === "map" ? r.title : r.kind === "style" ? `${r.style} (the style's average)` : r.file.replace(/^.*\//, "");

/** Mastering like a reference without the agent: pick a reference track (or a style's average),
    match, then compare the song before and after with the reference at the same loudness. */
export function MasterWindow({ window: w }: { window: OpenWindow }) {
  const songs = useMaps((s) => s.songs);
  const songStyle = useEngine((s) => s.doc?.style);
  const [reference, setReference] = useState<Reference | undefined>(last?.reference);
  const [target, setTarget] = useState<number | undefined>();
  const [bars, setBars] = useState<{ from?: number; to?: number }>({});
  const [running, setRunning] = useState<number>();
  const [result, setResult] = useState<Match | undefined>(last?.result);
  const [error, setError] = useState<string>();
  useStopPreviewsOnClose();

  useEffect(() => {
    void refreshSongs().catch(() => {});
  }, []);

  // The heard songs with their audio, by style; the song's own style first.
  const byStyle = useMemo(() => {
    const groups = new Map<string, { id: string; title: string }[]>();

    for (const s of songs)
      if (s.map && s.file && s.inUse)
        groups.set(s.style, [...(groups.get(s.style) ?? []), { id: s.id, title: s.title }]);

    return [...groups.entries()].sort(([a], [b]) => (b === songStyle ? 1 : 0) - (a === songStyle ? 1 : 0) || a.localeCompare(b));
  }, [songs, songStyle]);

  useEffect(() => {
    if (running === undefined) return;
    const timer = setInterval(() => setRunning((s) => (s ?? 0) + 1), 1000);
    return () => clearInterval(timer);
  }, [running !== undefined]);

  const match = async () => {
    if (!reference) return;
    setError(undefined);
    setRunning(0);
    void stopPreview();

    const params: Record<string, unknown> =
      reference.kind === "map" ? { id: reference.id } : reference.kind === "style" ? { style: reference.style } : { file: reference.file, from: reference.from, to: reference.from + 30 };

    if (target !== undefined) params.targetLufs = target;
    if (bars.from) params.start = (bars.from - 1) * 3840;
    if (bars.from && bars.to && bars.to >= bars.from) params.end = bars.to * 3840;

    try {
      const r = await call<Match>("master.match", params);
      last = { reference, result: r };
      setResult(r);
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setRunning(undefined);
    }
  };

  const chooseFile = () => void pickFile("A reference track", ["wav", "aif", "aiff", "flac", "mp3", "m4a"]).then((file) => {
    if (file) setReference({ kind: "file", file, from: 60 });
  });

  return (
    <FloatingWindow window={w} width={460} accent="#d6d6db" title={<><Wand2 size={13} className="text-dim" /> Master like a reference</>}>
      <div className="flex flex-col gap-3 p-3 text-xs">
        <p className="text-dim">
          Measures a reference's drop and your song's loudest part, then sets an EQ, a multiband compressor, mono bass and the limiter on the master
          until the song measures close. One undo takes it back.
        </p>

        <div className="grid grid-cols-[84px_1fr] items-center gap-x-2 gap-y-1.5">
          <span className="text-faint">Reference</span>
          <Dropdown>
            <DropdownTrigger asChild>
              <button type="button" className="flex h-7 min-w-0 items-center gap-1 rounded-[4px] bg-raised px-2 text-left ring-1 ring-line hover:ring-dim">
                <span className={cn("min-w-0 flex-1 truncate", !reference && "text-faint")}>{reference ? label(reference) : "Choose a track or a style…"}</span>
                <ChevronDown size={12} className="shrink-0 text-faint" />
              </button>
            </DropdownTrigger>
            <DropdownContent className="w-[360px]">
              {byStyle.length === 0 && <DropdownLabel>No heard reference tracks yet (the agent's Maps)</DropdownLabel>}
              {byStyle.map(([style, list]) => (
                <DropdownSub key={style} label={`${style} (${list.length})`}>
                  <DropdownItem onSelect={() => setReference({ kind: "style", style })}>The style's average</DropdownItem>
                  <DropdownSeparator />
                  {list.map((s) => <DropdownItem key={s.id} onSelect={() => setReference({ kind: "map", id: s.id, title: s.title })}>{s.title}</DropdownItem>)}
                </DropdownSub>
              ))}
              <DropdownSeparator />
              <DropdownItem onSelect={chooseFile}>A file…</DropdownItem>
            </DropdownContent>
          </Dropdown>

          {reference?.kind === "file" && (
            <>
              <span className="text-faint">Its drop from</span>
              <label className="flex items-center gap-1.5 text-dim">
                <input type="number" min={0} value={reference.from} onChange={(e) => setReference({ ...reference, from: Math.max(0, Number(e.target.value)) })}
                  className="h-6 w-16 rounded-[4px] bg-raised px-1.5 text-fg ring-1 ring-line tabular" />
                seconds, 30 seconds of it
              </label>
            </>
          )}

          <span className="text-faint">Loudness</span>
          <Dropdown>
            <DropdownTrigger asChild>
              <button type="button" className="flex h-7 items-center gap-1 rounded-[4px] bg-raised px-2 text-left ring-1 ring-line hover:ring-dim">
                <span className="flex-1">{LOUDNESS.find(([v]) => v === target)?.[1]}</span>
                <ChevronDown size={12} className="text-faint" />
              </button>
            </DropdownTrigger>
            <DropdownContent>
              {LOUDNESS.map(([v, text]) => <DropdownItem key={text} checked={v === target} onSelect={() => setTarget(v)}>{text}</DropdownItem>)}
            </DropdownContent>
          </Dropdown>

          <span className="text-faint">Song's part</span>
          <div className="flex items-center gap-1.5 text-dim">
            bars
            {(["from", "to"] as const).map((k) => (
              <input key={k} type="number" min={1} placeholder={k === "from" ? "auto" : ""} value={bars[k] ?? ""}
                onChange={(e) => setBars({ ...bars, [k]: e.target.value ? Math.max(1, Number(e.target.value)) : undefined })}
                className="h-6 w-14 rounded-[4px] bg-raised px-1.5 text-fg ring-1 ring-line tabular placeholder:text-faint" />
            ))}
            <span className="text-faint">{bars.from ? "" : "its loudest 16 bars"}</span>
          </div>
        </div>

        <div className="flex items-center gap-2">
          <Button tone="accent" active disabled={!reference || running !== undefined} onClick={() => void match()}>
            {running !== undefined ? <Loader2 size={13} className="animate-spin" /> : <Wand2 size={13} />}
            {running !== undefined ? "Matching…" : result ? "Match again" : "Match"}
          </Button>
          {running !== undefined && <span className="text-faint tabular">Rendering and measuring, up to five times: {running} s</span>}
        </div>

        {error && <div className="rounded-[4px] bg-rec/10 px-2 py-1.5 text-rec">{error}</div>}
        {result && <MatchResult result={result} />}
      </div>
    </FloatingWindow>
  );
}

function MatchResult({ result: r }: { result: Match }) {
  const rows: [string, (p: Profile) => string][] = [
    ["Loudness", (p) => `${p.lufs.toFixed(1)} LUFS`],
    ["Peaks above it", (p) => `${p.peakToLoudness.toFixed(1)} dB`],
    ...BANDS.map((b, i) => [`${b} punch`, (p: Profile) => `${p.punch[i].toFixed(1)} dB`] as [string, (p: Profile) => string]),
    ...BANDS.slice(1).map((b, i) => [`${b} width`, (p: Profile) => p.width[i + 1] <= -59 ? "mono" : `${p.width[i + 1].toFixed(0)} dB`] as [string, (p: Profile) => string]),
    ["Mono below", (p) => p.monoBelowHz > 0 ? `${p.monoBelowHz} Hz` : "never"],
  ];
  const advice = (r.balance ?? []).filter((b) => b.advice);

  return (
    <div className="flex flex-col gap-3 border-t border-line pt-3">
      <Listen files={r.files} lufs={r.filesLufs} />

      {advice.length > 0 && (
        <div className="rounded-[4px] bg-[#e0a84b]/10 px-2 py-1.5 text-[#e0a84b]">
          <div className="mb-0.5 font-medium">Mastering can't fix these: the mix can</div>
          {advice.map((b) => <div key={b.part}>• {b.advice}</div>)}
        </div>
      )}
      {r.loudnessNote && <div className="text-dim">{r.loudnessNote}</div>}

      <ToneCurves result={r} />

      <table className="w-full tabular">
        <thead>
          <tr className="text-2xs text-faint">
            <th className="text-left font-normal" />
            <th className="text-right font-normal">Before</th>
            <th className="text-right font-normal">After</th>
            <th className="text-right font-normal">Reference</th>
          </tr>
        </thead>
        <tbody>
          {rows.map(([name, value]) => (
            <tr key={name}>
              <td className="py-0.5 text-dim">{name}</td>
              <td className="text-right text-faint">{value(r.before)}</td>
              <td className="text-right text-fg">{value(r.after)}</td>
              <td className="text-right text-dim">{value(r.reference)}</td>
            </tr>
          ))}
        </tbody>
      </table>

      <div>
        <div className="mb-0.5 text-2xs uppercase tracking-wide text-faint">{r.close ? "Close to the reference" : "Still different"}</div>
        {r.remaining.length === 0 ? <div className="text-dim">Nothing worth naming.</div> : r.remaining.map((t) => <div key={t} className="text-dim">• {t}</div>)}
        <div className="mt-1 text-faint">{r.renders} renders. Less punch than the reference can't be put back here: compress less in the mix.</div>
      </div>
    </div>
  );
}

/** Before, after and the reference at one loudness: switching keeps the place. */
function Listen({ files, lufs }: { files: Match["files"]; lufs: number }) {
  const previewing = useEngine((s) => s.previewing);
  const started = useRef<{ at: number; from: number } | undefined>(undefined);

  const play = (path: string) => {
    if (previewing === path) {
      started.current = undefined;
      return void stopPreview();
    }

    const from = started.current && previewing ? started.current.from + (performance.now() - started.current.at) / 1000 : 0;
    started.current = { at: performance.now(), from };
    useEngine.setState({ previewing: path });
    void call("preview.play", { path, from });
  };

  return (
    <div className="flex items-center gap-1.5">
      {(["before", "after", "reference"] as const).map((k) => {
        const on = previewing === files[k];
        return (
          <button key={k} type="button" onClick={() => play(files[k])}
            className={cn("flex h-8 flex-1 items-center justify-center gap-1.5 rounded-[6px] text-xs capitalize ring-1",
              on ? "bg-accent/15 text-fg ring-accent" : "bg-raised text-dim ring-line hover:text-fg")}>
            {on ? <Square size={10} fill="currentColor" className="text-accent" /> : <Play size={11} />}
            {k}
          </button>
        );
      })}
      <span className="w-[62px] shrink-0 text-right text-2xs text-faint tabular" title="All three play at this loudness, so you judge the sound, not the level">
        at {lufs.toFixed(1)} LUFS
      </span>
    </div>
  );
}

/** The tonal balance of before, after and the reference, third octave by third octave. */
function ToneCurves({ result: r }: { result: Match }) {
  const width = 436, height = 90, top = 6, bottom = 14;
  const all = [r.before.tone, r.after.tone, r.reference.tone];
  const values = all.flat().filter((_, i) => i % 29 >= 1);
  const high = Math.max(...values) + 2, low = Math.max(Math.min(...values) - 2, high - 60);
  const x = (k: number) => (k / 28) * width;
  const y = (v: number) => top + ((high - Math.max(v, low)) / (high - low)) * (height - top - bottom);
  const path = (tone: number[]) => tone.map((v, k) => `${k ? "L" : "M"}${x(k).toFixed(1)},${y(v).toFixed(1)}`).join(" ");
  const marks: [number, string][] = [[6, "100"], [16, "1k"], [26, "10k"]];

  return (
    <div>
      <div className="mb-1 flex items-center gap-3 text-2xs text-faint">
        <span className="flex-1 uppercase tracking-wide">Tone</span>
        <span className="flex items-center gap-1"><span className="h-0.5 w-3 bg-faint" /> before</span>
        <span className="flex items-center gap-1"><span className="h-0.5 w-3 bg-accent" /> after</span>
        <span className="flex items-center gap-1"><span className="h-0.5 w-3 border-t border-dashed border-fg" /> reference</span>
      </div>
      <svg viewBox={`0 0 ${width} ${height}`} className="block w-full rounded-[4px] bg-bg" aria-label="Tonal balance before, after and of the reference">
        {marks.map(([k, text]) => (
          <g key={text}>
            <line x1={x(k)} x2={x(k)} y1={top} y2={height - bottom} className="stroke-line" />
            <text x={x(k) + 2} y={height - 3} className="fill-faint text-[9px]">{text} Hz</text>
          </g>
        ))}
        <path d={path(r.before.tone)} fill="none" className="stroke-faint" strokeWidth={1.5} />
        <path d={path(r.reference.tone)} fill="none" className="stroke-fg" strokeWidth={1.5} strokeDasharray="4 3" />
        <path d={path(r.after.tone)} fill="none" className="stroke-accent" strokeWidth={2} />
      </svg>
    </div>
  );
}
