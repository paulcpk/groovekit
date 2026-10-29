import { ArrowLeft, ArrowRight } from "lucide-react";
import { useEffect, useLayoutEffect, useState } from "react";
import { create } from "zustand";
import { Button } from "@/components/controls";
import { Dialog } from "@/components/dialog";
import { StarterSamples } from "@/features/browser/StarterSamples";
import { useStarter } from "@/features/browser/starter";
import { cn } from "@/lib/cn";
import { useEngine } from "@/state/engine";
import { setUi } from "@/state/ui";

// The welcome on the first start, and a short tour of the window: each step points at a part of
// the window marked data-tour and says what it's for. Settings → About shows it again.

const WELCOMED = "groovekit.welcomed";

/** Whether the welcome has been shown (and closed) on this Mac. */
export function isWelcomed() {
  try {
    return !!localStorage.getItem(WELCOMED);
  } catch {
    return true;
  }
}

/** step 0 is the welcome; 1.. the tour's stops; STOPS.length + 1 the end. */
export const useTour = create<{ open: boolean; step: number }>(() => ({ open: false, step: 0 }));

export const startTour = () => useTour.setState({ open: true, step: 0 });

interface Stop {
  target: string;
  title: string;
  text: string;
  /** Puts the window in the state the stop talks about. */
  prepare?: () => void;
}

const STOPS: Stop[] = [
  {
    target: "sidebar", title: "The Browser",
    text: "Your sample library. Search by name, or by how a sound sounds once GrooveKit can listen. Click a sample to hear it, drag it onto the arrangement or a channel. The starter samples land here.",
    prepare: () => setUi({ sidebarOpen: true, sidebarTab: "browser" }),
  },
  {
    target: "playlist", title: "The arrangement",
    text: "Rows of pattern clips, audio clips and automation. Click a lane to place the selected pattern, drag to move, Alt-drag to copy, ⌘ + scroll to zoom. Right-click any knob or fader in GrooveKit to automate it.",
  },
  {
    target: "dock", title: "Channel rack, piano roll and mixer",
    text: "The channel rack holds your instruments and their step sequencer, the piano roll edits notes, and the mixer has the inserts with effects, sends and faders. F6, F7 and F9 switch between them.",
    prepare: () => setUi({ bottomOpen: true, bottomPanel: "rack" }),
  },
  {
    target: "transport", title: "Transport",
    text: "Play, stop, record and loop. PAT loops the pattern you're working on, SONG plays the arrangement. Space plays and pauses, L loops.",
  },
  {
    target: "agent", title: "The agent",
    text: "Builds and mixes songs with you: it picks sounds by how they sound, matches them to the tracks you love and asks you to choose by ear. It needs your own Anthropic API key, which you enter in its tab.",
    prepare: () => setUi({ sidebarOpen: true, sidebarTab: "activity" }),
  },
];

function markWelcomed() {
  try {
    localStorage.setItem(WELCOMED, "1");
  } catch {
    // Without storage it shows again next time.
  }
}

/** Opens the welcome once, when the engine is first connected; ?tour in the URL shows it again. */
export function Welcome() {
  const connected = useEngine((s) => s.status === "connected");
  const { open, step } = useTour();

  useEffect(() => {
    if (!connected) return;
    if (!isWelcomed() || new URLSearchParams(location.search).has("tour")) startTour();
  }, [connected]);

  const close = () => {
    markWelcomed();
    useTour.setState({ open: false, step: 0 });
  };

  if (!open) return null;
  if (step === 0) return <WelcomeDialog onSkip={close} onTour={() => useTour.setState({ step: 1 })} />;
  return <Tour step={step} onClose={close} />;
}

function WelcomeDialog({ onSkip, onTour }: { onSkip: () => void; onTour: () => void }) {
  const starter = useStarter((s) => s.status);

  return (
    <Dialog open onOpenChange={(o) => !o && onSkip()} title="Welcome to GrooveKit" className="w-[480px]">
      <div className="flex flex-col gap-3 text-xs leading-relaxed text-dim">
        <p>
          GrooveKit makes electronic music with you: a sampler with layers and grains, a mixer with effects and sends, an arrangement,
          and an agent that builds, mixes and masters songs from your sample library and the tracks you love. Everything runs on this Mac.
        </p>
        <div className="rounded-[6px] bg-bg p-2.5 ring-1 ring-line">
          {starter?.installed
            ? <span>The starter samples are in your library: drum machines, one-shots and loops to begin with.</span>
            : <StarterSamples />}
        </div>
        <div className="flex items-center justify-end gap-2 pt-1">
          <Button size="sm" onClick={onSkip}>Skip</Button>
          <Button size="sm" active tone="accent" onClick={onTour}>Show me around <ArrowRight size={12} /></Button>
        </div>
      </div>
    </Dialog>
  );
}

/** The spot a stop points at, in window pixels. */
function useTarget(target: string | undefined, step: number) {
  const [rect, setRect] = useState<DOMRect | null>(null);

  useLayoutEffect(() => {
    if (!target) return setRect(null);
    const measure = () => {
      const el = document.querySelector(`[data-tour="${target}"]`);
      setRect(el ? el.getBoundingClientRect() : null);
    };
    // Measured again once the window has taken the state the stop asked for.
    measure();
    const timer = setTimeout(measure, 80);
    addEventListener("resize", measure);
    return () => {
      clearTimeout(timer);
      removeEventListener("resize", measure);
    };
  }, [target, step]);

  return rect;
}

const CARD_W = 340, GAP = 12;

function Tour({ step, onClose }: { step: number; onClose: () => void }) {
  const stop = STOPS[step - 1] as Stop | undefined;
  const last = step > STOPS.length;

  useEffect(() => stop?.prepare?.(), [stop]);
  const rect = useTarget(stop?.target, step);

  const go = (to: number) => {
    if (to > STOPS.length + 1) onClose();
    else useTour.setState({ step: Math.max(1, to) });
  };

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (e.key === "Escape") onClose();
      else if (e.key === "ArrowRight" || e.key === "Enter") go(step + 1);
      else if (e.key === "ArrowLeft") go(step - 1);
      else return;
      e.preventDefault();
      e.stopPropagation();
    };
    addEventListener("keydown", onKey, true);
    return () => removeEventListener("keydown", onKey, true);
  });

  // The card sits beside the spot: to its right when there's room, else to its left, else below
  // or above it; the end card in the middle.
  const place = (): React.CSSProperties => {
    if (!rect || last) return { left: "50%", top: "50%", transform: "translate(-50%, -50%)" };
    const W = innerWidth, H = innerHeight;
    let left: number, top: number;
    if (rect.right + GAP + CARD_W <= W) {
      left = rect.right + GAP;
      top = rect.top;
    } else if (rect.left - GAP - CARD_W >= 0) {
      left = rect.left - GAP - CARD_W;
      top = rect.top;
    } else {
      left = rect.left;
      top = rect.bottom + GAP + 200 <= H ? rect.bottom + GAP : rect.top - GAP - 200;
    }
    return { left: Math.min(Math.max(8, left), W - CARD_W - 8), top: Math.min(Math.max(8, top), H - 220) };
  };

  return (
    <div className="fixed inset-0 z-50">
      {/* The dark around the spot, with the spot itself clear. */}
      {rect && !last ? (
        <div className="absolute rounded-[6px] ring-2 ring-accent shadow-[0_0_0_9999px_rgba(0,0,0,0.55)] transition-all duration-200"
          style={{ left: rect.left - 4, top: rect.top - 4, width: rect.width + 8, height: rect.height + 8 }} />
      ) : (
        <div className="absolute inset-0 bg-black/55" />
      )}
      <div className={cn("absolute flex flex-col gap-2 rounded-lg border border-line bg-panel p-4 shadow-2xl")} style={{ width: CARD_W, ...place() }}>
        <div className="flex items-center">
          <div className="flex-1 text-sm font-semibold text-fg">{last ? "That's the tour" : stop?.title}</div>
          <span className="text-2xs text-faint">{Math.min(step, STOPS.length)} / {STOPS.length}</span>
        </div>
        <p className="text-xs leading-relaxed text-dim">
          {last ? "Find it again in Settings, under About. Have fun." : stop?.text}
        </p>
        <div className="flex items-center gap-2 pt-1">
          <button type="button" onClick={onClose} className="text-2xs text-faint hover:text-fg">Skip the tour</button>
          <div className="flex-1" />
          {step > 1 && <Button size="sm" onClick={() => go(step - 1)}><ArrowLeft size={12} /> Back</Button>}
          <Button size="sm" active tone="accent" onClick={() => go(step + 1)}>{last ? "Done" : <>Next <ArrowRight size={12} /></>}</Button>
        </div>
      </div>
    </div>
  );
}
