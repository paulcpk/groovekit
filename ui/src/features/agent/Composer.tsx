import { ArrowUp, Check, Download, Loader2, Mic, Square, X } from "lucide-react";
import { useCallback, useEffect, useLayoutEffect, useRef, useState } from "react";

import { Button, IconButton } from "@/components/controls";

import { cn } from "@/lib/cn";
import { toast } from "@/state/engine";

import { BAR_COUNT, downloadVoiceModel, loadVoiceModel, RECORDER_ERRORS, transcribe, transcriptText, useVoiceModel, useVoiceRecorder, useVoiceRequest } from "./voice";

export const VOICE_SHORTCUT = "⌘⇧M";

export const clock = (ms: number) => {
  const seconds = Math.floor(ms / 1000);
  return `${Math.floor(seconds / 60)}:${String(seconds % 60).padStart(2, "0")}`;
};

/** The message box: a field that grows with its text, voice messages (live level bars, then
    cancel, check the text first, or send it straight away), send, and stop while the agent works. */
export function Composer({ value, onChange, onSend, onStop, running, disabled }: {
  value: string; onChange: (v: string) => void; onSend: (text?: string) => void; onStop: () => void; running: boolean; disabled: boolean;
}) {
  const input = useRef<HTMLTextAreaElement>(null);
  const recorder = useVoiceRecorder();
  const model = useVoiceModel();
  const [transcribing, setTranscribing] = useState(false);
  const [offerModel, setOfferModel] = useState(false);
  // A recording a pick window started is that window's.
  const recording = recorder.state !== "idle" && recorder.owner === "composer";
  const level = recorder.levels[recorder.levels.length - 1] ?? 0;

  // Grows with its text, up to a point.
  useLayoutEffect(() => {
    const el = input.current;
    if (!el) return;
    el.style.height = "auto";
    el.style.height = `${Math.min(180, el.scrollHeight)}px`;
  }, [value, recording, transcribing]);

  useEffect(() => {
    if (!disabled) void loadVoiceModel(true).catch(() => {});
  }, [disabled]);

  useEffect(() => {
    if (recorder.error) toast(RECORDER_ERRORS[recorder.error], "error");
  }, [recorder.error]);

  // The model finished downloading: say so once.
  const wasDownloading = useRef(false);
  useEffect(() => {
    if (wasDownloading.current && model?.ready) {
      setOfferModel(false);
      void loadVoiceModel(true).catch(() => {});
      toast(`Voice is ready: click the microphone or press ${VOICE_SHORTCUT}`);
    }
    wasDownloading.current = !!model?.downloading;
  }, [model?.ready, model?.downloading]);

  const startVoice = useCallback(() => {
    if (disabled || transcribing) return;
    if (!model?.ready) {
      setOfferModel(true);
      return;
    }
    void loadVoiceModel(true).catch(() => {});
    void recorder.start();
  }, [disabled, transcribing, model?.ready, recorder]);

  /** Stops recording and turns it into text: into the field to check, or sent as it is. */
  const accept = useCallback(async (sendNow: boolean) => {
    const audio = await recorder.stop();
    if (!audio) return;
    setTranscribing(true);
    try {
      const text = transcriptText(await transcribe(audio));
      if (!text) {
        toast("Didn't catch that. Try again a little closer to the microphone.");
        return;
      }
      const combined = value.trim() ? `${value.trimEnd()} ${text}` : text;
      if (sendNow && !running) onSend(combined);
      else {
        onChange(combined);
        requestAnimationFrame(() => {
          input.current?.focus();
          input.current?.setSelectionRange(combined.length, combined.length);
        });
      }
    } catch (e) {
      toast(e instanceof Error ? e.message : String(e), "error");
    } finally {
      setTranscribing(false);
    }
  }, [recorder, value, running, onSend, onChange]);

  // The voice shortcut: starts recording, or finishes it.
  const request = useVoiceRequest((s) => s.pending);
  useEffect(() => {
    if (!request) return;
    useVoiceRequest.setState({ pending: false });
    if (recorder.state === "recording" && recorder.owner === "composer") void accept(false);
    else if (recorder.state === "idle") startVoice();
  }, [request, recorder.state, recorder.owner, accept, startVoice]);

  // While recording: Enter keeps the text to check, ⌘Enter sends it, Escape throws it away.
  useEffect(() => {
    if (!recording) return;
    const onKey = (e: KeyboardEvent) => {
      if (e.key !== "Enter" && e.key !== "Escape") return;
      e.preventDefault();
      e.stopPropagation();
      if (e.key === "Escape") recorder.cancel();
      else void accept(e.metaKey || e.ctrlKey);
    };
    window.addEventListener("keydown", onKey, true);
    return () => window.removeEventListener("keydown", onKey, true);
  }, [recording, recorder, accept]);

  const canSend = !!value.trim() && !disabled && !transcribing;

  return (
    <div className="shrink-0 p-2 pt-1">
      {offerModel && !model?.ready && <VoiceModelOffer onClose={() => setOfferModel(false)} />}
      <div
        className={cn(
          "relative rounded-[12px] bg-raised ring-1 transition-[box-shadow,ring-color] duration-150",
          recording ? "ring-agent/60" : "ring-line focus-within:ring-agent/50",
        )}
        // Glows with the voice while recording.
        style={recording ? { boxShadow: `0 0 ${6 + level * 26}px ${level * 6}px color-mix(in srgb, var(--color-agent) ${Math.round(18 + level * 40)}%, transparent)` } : undefined}
      >
        {recording ? (
          <div className="flex h-[72px] items-center gap-2 px-2.5">
            <span className={cn("h-2 w-2 shrink-0 rounded-full bg-rec", recorder.state === "recording" && "animate-pulse")} />
            <span className="w-8 shrink-0 text-[11px] text-dim tabular">{clock(recorder.elapsed)}</span>
            <div className="flex h-9 min-w-0 flex-1 items-center justify-end gap-[2px] overflow-hidden" role="img" aria-label="Recording">
              {recorder.levels.map((l, i) => (
                <span key={i} className="w-[2px] shrink-0 rounded-full bg-agent transition-[height] duration-100 ease-out"
                  style={{ height: `${Math.max(7, l * 100)}%`, opacity: 0.25 + (0.75 * i) / BAR_COUNT }} />
              ))}
            </div>
            <IconButton size="sm" title="Throw it away (Esc)" onClick={recorder.cancel} className="h-7 w-7 rounded-full"><X size={13} /></IconButton>
            <IconButton size="sm" title="Check the text before sending (Enter)" disabled={recorder.state !== "recording"} onClick={() => void accept(false)}
              className="h-7 w-7 rounded-full bg-control text-fg"><Check size={13} /></IconButton>
            <IconButton size="sm" title="Send (⌘Enter)" disabled={recorder.state !== "recording" || running} onClick={() => void accept(true)}
              className="h-7 w-7 rounded-full bg-agent text-black hover:bg-agent"><ArrowUp size={13} /></IconButton>
          </div>
        ) : (
          <>
            <textarea
              ref={input}
              rows={1}
              value={value}
              disabled={disabled || transcribing}
              placeholder={disabled ? "Waiting for the engine…" : transcribing ? "" : "Ask the agent…"}
              onChange={(e) => onChange(e.target.value)}
              onKeyDown={(e) => {
                e.stopPropagation();
                // Enter sends; Shift+Enter starts a new line.
                if (e.key === "Enter" && !e.shiftKey && !e.nativeEvent.isComposing) {
                  e.preventDefault();
                  if (canSend && !running) onSend();
                }
              }}
              className="block max-h-[180px] min-h-[38px] w-full resize-none bg-transparent px-3 pt-2.5 pb-1 text-xs leading-5 text-fg outline-none placeholder:text-faint focus-visible:outline-none disabled:opacity-60"
            />
            {transcribing && (
              <div className="pointer-events-none absolute top-2.5 left-3 flex items-center gap-1.5 text-xs">
                <span className="text-shimmer">Transcribing…</span>
              </div>
            )}
            <div className="flex items-center gap-1 px-1.5 pb-1.5">
              <span className="ml-1.5 min-w-0 flex-1 truncate text-[10px] text-faint">
                {value.trim() ? "Enter to send · Shift+Enter for a new line" : `${VOICE_SHORTCUT} to talk`}
              </span>
              <IconButton size="sm" title={`Voice message (${VOICE_SHORTCUT})`} disabled={disabled || transcribing} onClick={startVoice}
                className="h-7 w-7 rounded-full">
                {transcribing ? <Loader2 size={13} className="animate-spin" /> : <Mic size={13} />}
              </IconButton>
              {running ? (
                <IconButton size="sm" title="Stop" onClick={onStop} className="h-7 w-7 rounded-full bg-control text-fg"><Square size={10} fill="currentColor" /></IconButton>
              ) : (
                <IconButton size="sm" title="Send (Enter)" disabled={!canSend} onClick={() => onSend()}
                  className={cn("h-7 w-7 rounded-full transition-colors", canSend ? "bg-agent text-black hover:bg-agent" : "bg-control")}>
                  <ArrowUp size={13} />
                </IconButton>
              )}
            </div>
          </>
        )}
      </div>
    </div>
  );
}

/** Voice needs Whisper's model on this Mac: what it is, and downloading it once. */
export function VoiceModelOffer({ onClose }: { onClose: () => void }) {
  const model = useVoiceModel();
  const mb = Math.round((model?.modelBytes ?? 190e6) / 1e6);

  return (
    <div className="mb-2 animate-rise rounded-[10px] bg-raised p-2.5 text-xs ring-1 ring-line">
      <div className="flex items-start gap-2">
        <Mic size={13} className="mt-px shrink-0 text-agent" />
        <div className="min-w-0 flex-1 leading-relaxed text-dim">
          {model?.downloading ? (
            <>
              <div className="mb-1.5 text-fg">Downloading the speech model… {Math.round((model.progress ?? 0) * 100)}%</div>
              <div className="h-1 overflow-hidden rounded-full bg-control">
                <div className="h-full rounded-full bg-agent transition-[width] duration-200" style={{ width: `${(model.progress ?? 0) * 100}%` }} />
              </div>
            </>
          ) : (
            <>
              <span className="text-fg">Talk to the agent.</span> Your voice is turned into text on this Mac by Whisper, so nothing you say
              leaves it. That needs a speech model, downloaded once ({mb} MB).
              {model?.error && <div className="mt-1 text-rec">{model.error}</div>}
            </>
          )}
        </div>
        <IconButton size="sm" title="Not now" onClick={onClose} className="-mt-0.5 shrink-0"><X size={11} /></IconButton>
      </div>
      {!model?.downloading && (
        <div className="mt-2 flex justify-end">
          <Button size="sm" className="bg-agent text-black hover:bg-agent" onClick={() => void downloadVoiceModel().catch((e) => toast(String(e), "error"))}>
            <Download size={11} /> {model?.error ? "Try again" : `Download (${mb} MB)`}
          </Button>
        </div>
      )}
    </div>
  );
}
