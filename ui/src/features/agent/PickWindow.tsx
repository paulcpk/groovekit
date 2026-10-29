import { Play, Square } from "lucide-react";
import { useEffect, useRef, useState } from "react";
import { FloatingWindow } from "@/components/window";
import { stopPreview, useStopPreviewsOnClose } from "@/features/shell/actions";
import { cn } from "@/lib/cn";
import { call, listen, useEngine } from "@/state/engine";
import { closeWindow, openWindow, type OpenWindow } from "@/state/windows";

/** A choice the agent puts to the user: two to ten sounds to pick from by ear. */
export interface Offer {
  id: number;
  title: string;
  style?: string;
  reference?: { file: string; from?: number; title?: string };
  /** alone and rest: a candidate tried in the song, as two layers played in sync. */
  candidates: { path: string; audition?: string; label?: string; alone?: string; rest?: string }[];
}

// Offers arrive as "picks" notifications; a choice made anywhere closes the window.
const offers = new Map<number, Offer>();

listen("picks", (params: Offer | { id: number; choice: number }) => {
  if ("candidates" in params) {
    offers.set(params.id, params);
    openWindow({ kind: "pick", id: params.id });
  } else {
    offers.delete(params.id);
    closeWindow(`pick:${params.id}`);
  }
});

const LETTERS = "ABCDEFGHIJ".split("");
const name = (path: string) => path.replace(/^.*\//, "").replace(/\.[^.]+$/, "");

/** Plays a file (from a time in it), or stops it when it's the one playing. With layers, the sound
    plays alone with the rest of the song beneath it at underLevel. */
function usePlayer() {
  const previewing = useEngine((s) => s.previewing);
  const play = (file: string, from = 0, layers?: { alone: string; rest: string; underLevel: number }) => {
    if (previewing === file) return void stopPreview();
    useEngine.setState({ previewing: file });
    void call("preview.play", layers ? { path: layers.alone, under: layers.rest, underLevel: layers.underLevel, from } : { path: file, from });
  };
  return { previewing, play };
}

//==============================================================================
// The song under the sound, like a DJ mixer's cue mix: 0 the sound alone, 1 in the song.
const MIX_STORAGE = "groovekit.pickMix";

function loadMix() {
  try {
    const v = Number(localStorage.getItem(MIX_STORAGE) ?? "1");
    return v >= 0 && v <= 1 ? v : 1;
  } catch {
    return 1;
  }
}

/** The rest's level for a slider position: squared, so the first half of the way takes it well down. */
const underLevel = (mix: number) => mix * mix;

function MixSlider({ value, onChange, disabled }: { value: number; onChange: (v: number) => void; disabled: boolean }) {
  const track = useRef<HTMLDivElement>(null);
  const at = (clientX: number) => {
    const r = track.current!.getBoundingClientRect();
    return Math.min(1, Math.max(0, (clientX - r.left) / r.width));
  };

  return (
    <div className={cn("flex flex-col gap-1 rounded-[6px] bg-bg px-3 pb-2.5 pt-2 ring-1 ring-line", disabled && "opacity-40")}
      title={disabled ? "Only for sounds tried in the song" : "Fade the song under the sound. Double-click: in the song"}>
      <div className="flex justify-between text-2xs font-medium uppercase tracking-wide">
        <span className={cn(value < 0.5 ? "text-agent" : "text-faint")}>Sound alone</span>
        <span className="text-faint">{value > 0 && value < 1 ? `song ${Math.round(20 * Math.log10(underLevel(value)))} dB` : ""}</span>
        <span className={cn(value >= 0.5 ? "text-agent" : "text-faint")}>In the song</span>
      </div>
      <div ref={track} role="slider" tabIndex={disabled ? -1 : 0} aria-label="Song under the sound" aria-valuemin={0} aria-valuemax={100} aria-valuenow={Math.round(value * 100)}
        className="relative h-7 cursor-pointer touch-none outline-none focus-visible:ring-1 focus-visible:ring-agent"
        onPointerDown={(e) => {
          if (disabled) return;
          e.currentTarget.setPointerCapture(e.pointerId);
          onChange(at(e.clientX));
        }}
        onPointerMove={(e) => !disabled && e.currentTarget.hasPointerCapture(e.pointerId) && onChange(at(e.clientX))}
        onDoubleClick={() => !disabled && onChange(1)}
        onKeyDown={(e) => {
          e.stopPropagation();
          if (e.key === "ArrowLeft" || e.key === "ArrowDown") onChange(Math.max(0, value - 0.05));
          if (e.key === "ArrowRight" || e.key === "ArrowUp") onChange(Math.min(1, value + 0.05));
        }}>
        <div className="absolute inset-x-0 top-1/2 h-1.5 -translate-y-1/2 rounded-full bg-control" />
        <div className="absolute left-0 top-1/2 h-1.5 -translate-y-1/2 rounded-full bg-agent/60" style={{ width: `${value * 100}%` }} />
        <div className="absolute top-1/2 h-6 w-3.5 -translate-x-1/2 -translate-y-1/2 rounded-[4px] bg-fg shadow ring-1 ring-black/40" style={{ left: `${value * 100}%` }} />
      </div>
    </div>
  );
}

export function PickWindow({ window: w, id }: { window: OpenWindow; id: number }) {
  const [offer, setOffer] = useState<Offer | undefined>(() => offers.get(id));
  const { previewing, play } = usePlayer();
  const [other, setOther] = useState(false);
  const [note, setNote] = useState("");
  const [mix, setMixState] = useState(loadMix);
  const sent = useRef(0);
  useStopPreviewsOnClose();

  // Heard at once while a candidate plays; remembered for the next pick.
  const setMix = (v: number) => {
    setMixState(v);
    try {
      localStorage.setItem(MIX_STORAGE, String(v));
    } catch {
      // Kept for this window.
    }
    const now = performance.now();
    if (now - sent.current > 25 || v === 0 || v === 1) {
      sent.current = now;
      void call("preview.setUnderLevel", { level: underLevel(v) });
    }
  };
  const layered = offer?.candidates.some((c) => c.alone && c.rest) ?? false;

  // Opened again after a reload: ask the engine.
  useEffect(() => {
    if (!offer) void call<{ offer: Offer; status: string }>("picks.get", { id }).then((r) => (r.status === "waiting" ? setOffer(r.offer) : closeWindow(w.key)), () => closeWindow(w.key));
  }, [id, offer, w.key]);

  // "Other" goes back to the agent with what the user wants instead.
  const choose = (choice: number, text?: string) => {
    void stopPreview();
    void call("picks.choose", { id, choice, ...(text ? { note: text } : {}) }).then(() => closeWindow(w.key), () => {});
  };
  const sendOther = () => note.trim() && choose(-1, note.trim());

  // ↑ and ↓ move through the candidates and play each from the top; Enter picks; Space plays or stops.
  const [selected, setSelected] = useState(-1);
  const body = useRef<HTMLDivElement>(null);
  const rows = useRef<(HTMLDivElement | null)[]>([]);
  useEffect(() => body.current?.focus(), [offer]);

  const playCandidate = (i: number) => {
    const c = offer!.candidates[i];
    setSelected(i);

    // A candidate tried in the song already has the song in it: the transport would play it twice, out of step.
    if (c.alone && c.rest) {
      if (useEngine.getState().transport.playing) void call("transport.stop");
      play(c.audition ?? c.path, 0, { alone: c.alone, rest: c.rest, underLevel: underLevel(mix) });
    } else {
      play(c.audition ?? c.path);
    }
  };

  const onKeyDown = (e: React.KeyboardEvent) => {
    if (!offer || e.target instanceof HTMLTextAreaElement || e.target instanceof HTMLInputElement) return;
    const count = offer.candidates.length;
    const move = e.key === "ArrowDown" ? 1 : e.key === "ArrowUp" ? -1 : 0;

    if (move) {
      const next = selected < 0 ? (move > 0 ? 0 : count - 1) : (selected + move + count) % count;
      if (next !== selected || previewing !== (offer.candidates[next].audition ?? offer.candidates[next].path)) playCandidate(next);
      rows.current[next]?.scrollIntoView({ block: "nearest" });
    } else if (e.key === "Enter" && selected >= 0) {
      choose(selected);
    } else if (e.key === " ") {
      playCandidate(selected < 0 ? 0 : selected);
    } else {
      return;
    }

    e.preventDefault();
    e.stopPropagation();
  };

  return (
    <FloatingWindow window={w} width={420} accent="var(--color-agent)"
      title={<><span className="truncate">{offer?.title ?? "A choice"}</span><span className="shrink-0 text-2xs font-normal text-faint">Pick by ear</span></>}>
      {offer ? (
        <div ref={body} tabIndex={-1} onKeyDown={onKeyDown} className="flex flex-col gap-2 p-3 outline-none">
          <MixSlider value={mix} onChange={setMix} disabled={!layered} />
          {offer.reference && (
            <button type="button" onClick={() => play(offer.reference!.file, offer.reference!.from)}
              className={cn("flex h-9 items-center gap-2 rounded-[6px] px-2.5 text-left ring-1 transition-colors",
                previewing === offer.reference.file ? "bg-agent/15 ring-agent" : "bg-bg ring-line hover:ring-dim")}>
              {previewing === offer.reference.file ? <Square size={11} className="shrink-0 text-agent" fill="currentColor" /> : <Play size={12} className="shrink-0 text-dim" />}
              <span className="text-2xs uppercase tracking-wide text-faint">Reference</span>
              <span className="min-w-0 flex-1 truncate text-xs text-fg">{offer.reference.title ?? name(offer.reference.file)}</span>
            </button>
          )}
          <div className="-mx-1 flex max-h-[52vh] flex-col gap-2 overflow-y-auto px-1 py-0.5">
          {offer.candidates.map((c, i) => {
            const file = c.audition ?? c.path;
            const playing = previewing === file;
            return (
              <div key={c.path} ref={(el) => void (rows.current[i] = el)}
                className={cn("flex items-center gap-2 rounded-[6px] px-2 py-1.5 ring-1", playing ? "bg-agent/10" : "bg-raised", selected === i ? "ring-agent" : playing ? "ring-agent/60" : "ring-line")}>
                <button type="button" title={c.alone ? "Play it in the song" : c.audition ? "Play it in the groove" : "Play it"}
                  onClick={() => {
                    playCandidate(i);
                    body.current?.focus();
                  }}
                  className="flex h-8 w-8 shrink-0 items-center justify-center rounded-[6px] bg-control text-sm font-semibold text-fg hover:bg-hover">
                  {playing ? <Square size={11} className="text-agent" fill="currentColor" /> : LETTERS[i]}
                </button>
                <div className="min-w-0 flex-1">
                  <div className="truncate text-xs text-fg" title={c.path}>{c.label ?? name(c.path)}</div>
                  {c.label && <div className="truncate text-2xs text-faint">{name(c.path)}</div>}
                </div>
                <button type="button" onClick={() => choose(i)} className="h-6 shrink-0 rounded-[4px] bg-agent px-2.5 text-xs text-black hover:brightness-110">Pick</button>
              </div>
            );
          })}
          </div>
          {other ? (
            <div className="flex flex-col gap-1.5 pt-1">
              <textarea autoFocus rows={3} value={note} onChange={(e) => setNote(e.target.value)}
                onKeyDown={(e) => {
                  e.stopPropagation();
                  if (e.key === "Enter" && !e.shiftKey) {
                    e.preventDefault();
                    sendOther();
                  }
                  if (e.key === "Escape") setOther(false);
                }}
                placeholder="What should it look for instead? e.g. only hat loops, not whole drum loops"
                className="w-full resize-none rounded-[6px] bg-bg px-2 py-1.5 text-xs text-fg outline-none ring-1 ring-line placeholder:text-faint focus:ring-agent" />
              <div className="flex items-center gap-2">
                <span className="flex-1 text-2xs text-faint">The agent searches again with this. Nothing counts against these sounds.</span>
                <button type="button" onClick={() => setOther(false)} className="h-6 rounded-[4px] px-2 text-xs text-dim hover:bg-control hover:text-fg">Cancel</button>
                <button type="button" disabled={!note.trim()} onClick={sendOther}
                  className="h-6 rounded-[4px] bg-agent px-2.5 text-xs text-black hover:brightness-110 disabled:opacity-40">Send to agent</button>
              </div>
            </div>
          ) : (
            <div className="flex items-center gap-1 pt-1">
              <span className="flex-1 text-2xs text-faint" title={`Your pick counts as fitting ${offer.style ?? "the style"} from now on`}>↑↓ to listen, Enter to pick</span>
              <button type="button" onClick={() => setOther(true)} className="h-6 rounded-[4px] px-2 text-xs text-dim hover:bg-control hover:text-fg">Other…</button>
              <button type="button" onClick={() => choose(-1)} className="h-6 rounded-[4px] px-2 text-xs text-dim hover:bg-control hover:text-fg">None of these</button>
            </div>
          )}
        </div>
      ) : (
        <div className="p-3 text-xs text-faint">…</div>
      )}
    </FloatingWindow>
  );
}
