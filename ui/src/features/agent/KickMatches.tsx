import { Play, Search, Square } from "lucide-react";
import { useState } from "react";
import { previewSample, stopPreview } from "@/features/shell/actions";
import { cn } from "@/lib/cn";
import { call, useEngine } from "@/state/engine";
import { matchKicks, type KickMatch, type KickSound } from "./maps";

const NOTES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
const note = (hz: number) => {
  const midi = Math.round(69 + 12 * Math.log2(hz / 440));
  return `${NOTES[((midi % 12) + 12) % 12]}${Math.floor(midi / 12) - 1}`;
};

/** A reference's kick and rumble in numbers, its stretch to play, and the library's samples that
    sound most like it. */
export function KickMatches({ id, kick, file, from }: {
  id: string; kick: { sound: KickSound; bars: [number, number]; clean: boolean }; file?: string; from?: number;
}) {
  const [results, setResults] = useState<KickMatch[]>();
  const [loops, setLoops] = useState<boolean>();
  const [searching, setSearching] = useState(false);
  const [measuring, setMeasuring] = useState(false);
  const previewing = useEngine((s) => s.previewing);
  const k = kick.sound;

  const find = (only?: boolean) => {
    setLoops(only);
    setSearching(true);
    void matchKicks(id, only).then((r) => {
      setResults(r.results);
      setMeasuring(r.measuring);
    }, () => {}).finally(() => setSearching(false));
  };

  const playStretch = () => {
    if (!file) return;
    if (previewing === file) return void stopPreview();
    useEngine.setState({ previewing: file });
    void call("preview.play", { path: file, from: from ?? 0 });
  };

  const facts: [string, string][] = [
    ["Body", k.bodyHz > 0 ? `${Math.round(k.bodyHz)} Hz · ${note(k.bodyHz)}` : "?"],
    ["Rings", `${Math.round(k.decayMs)} ms`],
    ["Click", `${k.clickDb.toFixed(0)} dB`],
    ["Grit", `${k.gritDb.toFixed(0)} dB`],
    ["Rumble", k.rumbleDb !== undefined ? `${k.rumbleDb.toFixed(0)} dB` : "none"],
  ];

  return (
    <div>
      <div className="mb-1.5 flex items-center gap-2 text-2xs text-dim">
        <span className="flex-1">
          Bars {kick.bars[0] + 1}-{kick.bars[1]}{kick.clean ? ", with nothing but the low end" : ", the quietest stretch around it"}
          {k.pattern && <span className="ml-1 font-mono text-faint">{k.pattern}</span>}
        </span>
        {file && (
          <button type="button" onClick={playStretch} className="flex items-center gap-1 text-agent hover:text-fg">
            {previewing === file ? <Square size={9} fill="currentColor" /> : <Play size={10} />} Play it
          </button>
        )}
      </div>
      <div className="grid grid-cols-5 gap-1">
        {facts.map(([label, value]) => (
          <div key={label} className="rounded-[6px] bg-bg px-1.5 py-1 ring-1 ring-line">
            <div className="text-2xs text-faint">{label}</div>
            <div className="truncate text-xs text-fg tabular">{value}</div>
          </div>
        ))}
      </div>

      <div className="mt-2 flex items-center gap-1">
        <button type="button" onClick={() => find(loops)} disabled={searching}
          className="flex h-6 items-center gap-1.5 rounded-[4px] bg-raised px-2 text-xs text-dim ring-1 ring-line hover:text-fg hover:ring-agent/50 disabled:opacity-50">
          <Search size={11} /> {searching ? "Listening…" : "Find kicks like it"}
        </button>
        {results && ([[undefined, "All"], [false, "One-shots"], [true, "Loops"]] as const).map(([value, label]) => (
          <button key={label} type="button" onClick={() => find(value)}
            className={cn("h-6 rounded-[4px] px-1.5 text-2xs", loops === value ? "bg-control text-fg" : "text-dim hover:text-fg")}>{label}</button>
        ))}
      </div>

      {results && (
        <div className="mt-1.5 flex flex-col">
          {results.length === 0 && (
            <div className="text-2xs text-faint">{measuring ? "Your library's kicks are still being measured." : "No kicks measured in your library yet."}</div>
          )}
          {results.map((r) => (
            <div key={r.path} className="group flex items-start gap-2 rounded-[4px] px-1 py-1 hover:bg-hover" title={`${r.path}\n${r.kick}`}>
              <button type="button" onClick={() => void previewSample(r.path, 4)}
                className="mt-0.5 flex h-5 w-5 shrink-0 items-center justify-center rounded-[4px] bg-control text-dim hover:text-fg">
                {previewing === r.path ? <Square size={8} className="text-agent" fill="currentColor" /> : <Play size={9} />}
              </button>
              <div className="min-w-0 flex-1">
                <div className="flex items-baseline gap-1.5">
                  <span className="min-w-0 flex-1 truncate text-xs text-fg">{r.name}</span>
                  {r.loop && <span className="shrink-0 text-2xs text-faint">loop</span>}
                  <span className="shrink-0 text-2xs text-agent tabular">{Math.round(r.score)}</span>
                </div>
                {r.differences.length > 0 && <div className="truncate text-2xs text-faint">{r.differences.join(" · ")}</div>}
              </div>
            </div>
          ))}
        </div>
      )}
    </div>
  );
}
