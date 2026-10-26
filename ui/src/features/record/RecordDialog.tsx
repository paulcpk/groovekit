import type { Project } from "@groovekit/schema/project";
import type { Command } from "@groovekit/schema/protocol";
import { Circle, Square } from "lucide-react";
import { useEffect, useState } from "react";
import { Button, DragNumber } from "@/components/controls";
import { Dialog } from "@/components/dialog";
import { cn } from "@/lib/cn";
import { call, engine, execute, toast, useEngine } from "@/state/engine";
import { setUi, useUi } from "@/state/ui";

/** Picks inputs to record, each into its own playlist track, from a bar for a number of bars. */
export function RecordDialog() {
  const open = useUi((s) => s.recordOpen);
  const doc = useEngine((s) => s.doc);
  const recording = useEngine((s) => s.transport.recording);
  const [inputs, setInputs] = useState<string[]>([]);
  const [chosen, setChosen] = useState<Set<string>>(new Set());
  const [fromBar, setFromBar] = useState(1);
  const [bars, setBars] = useState(8);
  const [timed, setTimed] = useState(true);
  const [dropSilent, setDropSilent] = useState(true);
  const [normalize, setNormalize] = useState(true);

  useEffect(() => {
    if (open) engine.call<{ inputs: string[] }>("record.inputs").then((r) => setInputs(r.inputs), () => {});
  }, [open]);

  const toggle = (input: string) => setChosen((s) => {
    const n = new Set(s);
    if (n.has(input)) n.delete(input); else n.add(input);
    return n;
  });

  const digitakt = inputs.filter((i) => /^Track \d+$/.test(i) || ["Delay", "Reverb", "Chorus"].includes(i));

  const start = async () => {
    if (!doc || chosen.size === 0) return;
    const targets = await ensureTracks(doc, [...chosen]);
    await call("record.start", {
      targets, fromBar, ...(timed ? { bars } : {}), dropSilent, ...(normalize ? { normalizeLufs: -20 } : {}),
    });
    setUi({ recordOpen: false });
    toast(timed ? `Recording ${bars} bars from bar ${fromBar}` : "Recording; stop with the transport");
  };

  return (
    <Dialog open={open} onOpenChange={(o) => setUi({ recordOpen: o })} title="Record">
      <div className="mb-2 flex items-center gap-2 text-2xs text-faint">
        <span className="flex-1 uppercase tracking-wide">Inputs</span>
        {digitakt.length > 0 && <Button size="sm" onClick={() => setChosen(new Set(digitakt))}>All Digitakt tracks</Button>}
        <Button size="sm" onClick={() => setChosen(new Set())}>None</Button>
      </div>
      <div className="mb-4 grid max-h-[240px] grid-cols-2 gap-1 overflow-y-auto">
        {inputs.map((input) => (
          <button key={input} type="button" onClick={() => toggle(input)}
            className={cn("flex h-7 items-center gap-2 rounded-[4px] px-2 text-left text-xs", chosen.has(input) ? "bg-rec/20 text-fg ring-1 ring-rec/50" : "bg-raised text-dim hover:text-fg")}>
            <Circle size={9} className={chosen.has(input) ? "fill-rec text-rec" : "text-faint"} />
            <span className="truncate">{input}</span>
          </button>
        ))}
        {inputs.length === 0 && <div className="col-span-2 text-xs text-faint">No inputs. Pick an input device in Settings.</div>}
      </div>

      <div className="mb-4 flex flex-wrap items-center gap-x-5 gap-y-2 text-xs">
        <label className="flex items-center gap-1.5 text-dim">From bar <DragNumber value={fromBar} min={1} max={999} className="text-fg" onCommit={setFromBar} /></label>
        <label className="flex items-center gap-1.5 text-dim">
          <input type="checkbox" checked={timed} onChange={(e) => setTimed(e.target.checked)} className="accent-[var(--color-accent)]" />
          Stop after <DragNumber value={bars} min={1} max={512} className="text-fg" onCommit={setBars} /> bars
        </label>
        <label className="flex items-center gap-1.5 text-dim">
          <input type="checkbox" checked={dropSilent} onChange={(e) => setDropSilent(e.target.checked)} className="accent-[var(--color-accent)]" /> Skip silent takes
        </label>
        <label className="flex items-center gap-1.5 text-dim">
          <input type="checkbox" checked={normalize} onChange={(e) => setNormalize(e.target.checked)} className="accent-[var(--color-accent)]" /> Even out levels
        </label>
      </div>

      <div className="flex items-center gap-2">
        {recording ? (
          <Button tone="rec" active onClick={() => void call("record.stop")}><Square size={11} fill="currentColor" /> Stop recording</Button>
        ) : (
          <Button tone="rec" active disabled={chosen.size === 0} onClick={() => void start()}><Circle size={11} fill="currentColor" /> Record {chosen.size || ""}</Button>
        )}
        <div className="flex-1" />
        <Button size="sm" disabled={chosen.size !== 1 || recording} title="With the hardware playing a clean kick on this input, in time with GrooveKit's clock"
          onClick={() => void call("record.calibrate", { input: [...chosen][0], bars: 2 }).then((r) => toast(`Alignment ${r.alignmentMs} ms (spread ${r.spreadMs} ms over ${r.hits} hits)`))}>
          Calibrate
        </Button>
      </div>
      <div className="mt-3 text-2xs text-faint">Each input records into its own playlist track. GrooveKit starts clock-synced hardware with the take (Settings → MIDI clock).</div>
    </Dialog>
  );
}

/** A playlist track per input, named after it: one of that name, or the next empty one below
    the song's clips. */
async function ensureTracks(doc: Project, inputs: string[]) {
  const commands: Command[] = [];
  const taken = new Set([...doc.playlist.clips.map((c) => c.track), ...doc.playlist.tracks.flatMap((t, i) => (t.name ? [i] : []))]);
  let next = Math.max(-1, ...taken) + 1;
  const targets = inputs.map((input) => {
    const existing = doc.playlist.tracks.findIndex((t) => t.name === input);
    if (existing >= 0) return { input, track: existing };
    const track = next++;
    commands.push({ type: "updateTrack", track, changes: { name: input } });
    return { input, track };
  });
  if (commands.length) await execute(commands, { label: `Add ${commands.length} track${commands.length === 1 ? "" : "s"} for recording` });
  return targets;
}
