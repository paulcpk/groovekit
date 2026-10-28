import { Play, Square } from "lucide-react";
import { useEffect, useState } from "react";
import { FloatingWindow } from "@/components/window";
import { stopPreview, useStopPreviewsOnClose } from "@/features/shell/actions";
import { cn } from "@/lib/cn";
import { call, listen, useEngine } from "@/state/engine";
import { closeWindow, openWindow, type OpenWindow } from "@/state/windows";

/** A choice the agent puts to the user: two to four sounds to pick from by ear. */
export interface Offer {
  id: number;
  title: string;
  style?: string;
  reference?: { file: string; from?: number; title?: string };
  candidates: { path: string; audition?: string; label?: string }[];
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

const LETTERS = ["A", "B", "C", "D"];
const name = (path: string) => path.replace(/^.*\//, "").replace(/\.[^.]+$/, "");

/** Plays a file (from a time in it), or stops it when it's the one playing. */
function usePlayer() {
  const previewing = useEngine((s) => s.previewing);
  const play = (file: string, from = 0) => {
    if (previewing === file) return void stopPreview();
    useEngine.setState({ previewing: file });
    void call("preview.play", { path: file, from });
  };
  return { previewing, play };
}

export function PickWindow({ window: w, id }: { window: OpenWindow; id: number }) {
  const [offer, setOffer] = useState<Offer | undefined>(() => offers.get(id));
  const { previewing, play } = usePlayer();
  useStopPreviewsOnClose();

  // Opened again after a reload: ask the engine.
  useEffect(() => {
    if (!offer) void call<{ offer: Offer; status: string }>("picks.get", { id }).then((r) => (r.status === "waiting" ? setOffer(r.offer) : closeWindow(w.key)), () => closeWindow(w.key));
  }, [id, offer, w.key]);

  const choose = (choice: number) => {
    void stopPreview();
    void call("picks.choose", { id, choice }).then(() => closeWindow(w.key), () => {});
  };

  return (
    <FloatingWindow window={w} width={420} accent="var(--color-agent)"
      title={<><span className="truncate">{offer?.title ?? "A choice"}</span><span className="shrink-0 text-2xs font-normal text-faint">Pick by ear</span></>}>
      {offer ? (
        <div className="flex flex-col gap-2 p-3">
          {offer.reference && (
            <button type="button" onClick={() => play(offer.reference!.file, offer.reference!.from)}
              className={cn("flex h-9 items-center gap-2 rounded-[6px] px-2.5 text-left ring-1 transition-colors",
                previewing === offer.reference.file ? "bg-agent/15 ring-agent" : "bg-bg ring-line hover:ring-dim")}>
              {previewing === offer.reference.file ? <Square size={11} className="shrink-0 text-agent" fill="currentColor" /> : <Play size={12} className="shrink-0 text-dim" />}
              <span className="text-2xs uppercase tracking-wide text-faint">Reference</span>
              <span className="min-w-0 flex-1 truncate text-xs text-fg">{offer.reference.title ?? name(offer.reference.file)}</span>
            </button>
          )}
          {offer.candidates.map((c, i) => {
            const file = c.audition ?? c.path;
            const playing = previewing === file;
            return (
              <div key={c.path} className={cn("flex items-center gap-2 rounded-[6px] px-2 py-1.5 ring-1", playing ? "bg-agent/10 ring-agent/60" : "bg-raised ring-line")}>
                <button type="button" title={c.audition ? "Play it in the groove" : "Play it"} onClick={() => play(file)}
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
          <div className="flex items-center gap-2 pt-1">
            <span className="flex-1 text-2xs text-faint">Your pick counts as fitting {offer.style ?? "the style"} from now on.</span>
            <button type="button" onClick={() => choose(-1)} className="h-6 rounded-[4px] px-2 text-xs text-dim hover:bg-control hover:text-fg">None of these</button>
          </div>
        </div>
      ) : (
        <div className="p-3 text-xs text-faint">…</div>
      )}
    </FloatingWindow>
  );
}
