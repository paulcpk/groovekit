// Voice messages for the agent: the microphone is recorded here, and the engine turns it into text
// with Whisper on this Mac (the model is downloaded once). Nothing goes anywhere else.

import { useCallback, useEffect, useRef, useState } from "react";
import { create } from "zustand";
import { engine, listen } from "@/state/engine";

/** Whisper listens at 16 kHz, mono. */
const RATE = 16000;
/** Bars shown while recording; each one is the loudness of about 43 ms. */
export const BAR_COUNT = 64;
/** Recordings stop on their own after five minutes. */
export const MAX_RECORDING_MS = 5 * 60 * 1000;

export type RecorderState = "idle" | "starting" | "recording";
export type RecorderError = "denied" | "unsupported" | "failed";

export const RECORDER_ERRORS: Record<RecorderError, string> = {
  denied: "GrooveKit isn't allowed to use the microphone. Allow it in System Settings → Privacy & Security → Microphone.",
  unsupported: "Recording isn't available here.",
  failed: "The microphone couldn't start.",
};

/** Loudness of a block of samples as 0..1, boosted so speech fills the bar. */
function levelOf(samples: Float32Array) {
  let sum = 0;
  for (const value of samples) sum += value * value;
  return Math.min(1, Math.sqrt(sum / Math.max(1, samples.length)) * 5);
}

/** Brings a block down to 16 kHz, averaging the samples each output sample covers (which also
    keeps what's above 8 kHz from folding back into speech). `phase` carries over between blocks. */
function downsample(input: Float32Array, ratio: number, state: { phase: number; sum: number; count: number }) {
  const out: number[] = [];
  for (const value of input) {
    state.sum += value;
    state.count++;
    state.phase += 1;
    if (state.phase >= ratio) {
      state.phase -= ratio;
      out.push(state.sum / state.count);
      state.sum = 0;
      state.count = 0;
    }
  }
  return new Float32Array(out);
}

/** 16 kHz mono float samples as 16-bit PCM in base64, as the engine takes them. */
function toBase64Pcm(chunks: Float32Array[]) {
  const length = chunks.reduce((n, c) => n + c.length, 0);
  const pcm = new Int16Array(length);
  let at = 0;
  for (const chunk of chunks) {
    for (const value of chunk) pcm[at++] = Math.max(-32768, Math.min(32767, Math.round(value * 32767)));
  }
  const bytes = new Uint8Array(pcm.buffer);
  let binary = "";
  for (let i = 0; i < bytes.length; i += 0x8000) binary += String.fromCharCode(...bytes.subarray(i, i + 0x8000));
  return btoa(binary);
}

/**
 * Records a voice message and measures its loudness for the animation. `stop()` resolves with the
 * recording (base64 PCM, or null if nothing was said), `cancel()` throws it away. The microphone
 * is released as soon as the recording ends, or when the component goes.
 */
export function useVoiceRecorder() {
  const [state, setState] = useState<RecorderState>("idle");
  const [error, setError] = useState<RecorderError | null>(null);
  const [levels, setLevels] = useState<number[]>(() => Array(BAR_COUNT).fill(0));
  const [elapsed, setElapsed] = useState(0);
  const session = useRef<{
    stream: MediaStream;
    context: AudioContext;
    processor: ScriptProcessorNode;
    chunks: Float32Array[];
    timer: ReturnType<typeof setTimeout>;
  } | null>(null);
  // Cancelled while the microphone was still starting.
  const cancelled = useRef(false);
  const stopRef = useRef<() => Promise<string | null>>(async () => null);

  const release = useCallback(() => {
    const current = session.current;
    if (!current) return;
    session.current = null;
    clearTimeout(current.timer);
    current.processor.onaudioprocess = null;
    current.processor.disconnect();
    current.stream.getTracks().forEach((track) => track.stop());
    void current.context.close().catch(() => {});
    setState("idle");
    setLevels(Array(BAR_COUNT).fill(0));
    setElapsed(0);
  }, []);

  useEffect(() => release, [release]);

  const stop = useCallback(async () => {
    const current = session.current;
    if (!current) return null;
    const chunks = current.chunks;
    release();
    const samples = chunks.reduce((n, c) => n + c.length, 0);
    return samples > RATE / 4 ? toBase64Pcm(chunks) : null;
  }, [release]);
  stopRef.current = stop;

  const start = useCallback(async () => {
    if (session.current) return;
    setError(null);
    if (!navigator.mediaDevices?.getUserMedia) {
      setError("unsupported");
      return;
    }
    setState("starting");
    cancelled.current = false;
    let stream: MediaStream;
    try {
      stream = await navigator.mediaDevices.getUserMedia({ audio: { channelCount: 1, echoCancellation: true, noiseSuppression: true } });
    } catch (cause) {
      setState("idle");
      setError((cause as DOMException)?.name === "NotAllowedError" ? "denied" : "failed");
      return;
    }
    if (cancelled.current) {
      stream.getTracks().forEach((track) => track.stop());
      return;
    }
    try {
      const context = new AudioContext();
      const source = context.createMediaStreamSource(stream);
      // About 43 ms a block at 48 kHz: one bar each.
      const processor = context.createScriptProcessor(2048, 1, 1);
      const chunks: Float32Array[] = [];
      const startedAt = performance.now();
      const resampling = { phase: 0, sum: 0, count: 0 };
      const ratio = context.sampleRate / RATE;

      processor.onaudioprocess = (event) => {
        const input = event.inputBuffer.getChannelData(0);
        chunks.push(downsample(input, ratio, resampling));
        const level = levelOf(input);
        setLevels((previous) => [...previous.slice(1), level]);
        setElapsed(performance.now() - startedAt);
      };
      source.connect(processor);
      // A script processor only runs while connected to the output; it writes silence.
      processor.connect(context.destination);

      session.current = { stream, context, processor, chunks, timer: setTimeout(() => void stopRef.current(), MAX_RECORDING_MS) };
      setState("recording");
    } catch {
      stream.getTracks().forEach((track) => track.stop());
      setState("idle");
      setError("failed");
    }
  }, []);

  const cancel = useCallback(() => {
    if (!session.current) {
      cancelled.current = true;
      setState("idle");
      return;
    }
    release();
  }, [release]);

  return { state, error, levels, elapsed, start, stop, cancel };
}

//==============================================================================
export interface VoiceModel {
  ready: boolean;
  downloading: boolean;
  progress: number;
  modelBytes: number;
  error?: string;
}

/** Whether the speech model is on this Mac, or on its way. */
export const useVoiceModel = create<VoiceModel | null>(() => null);

listen("voiceModel", (status: VoiceModel) => useVoiceModel.setState(status, true));

/** The model's status; with prepare, the engine also loads it, so the first message isn't slow. */
export async function loadVoiceModel(prepare = false) {
  const status = await engine.call<VoiceModel>("voice.status", { prepare });
  useVoiceModel.setState(status, true);
  return status;
}

export async function downloadVoiceModel() {
  useVoiceModel.setState(await engine.call<VoiceModel>("voice.download"), true);
}

/** What the engine heard in a recording: its text, and the melody hummed or sung in it, if any
    (the agent places it with the melody tool, by the recording's id). */
export interface Transcript {
  text: string;
  recording?: string;
  melody?: { from: number; to: number; notes: number };
}

/** The text of a recording. The engine answers at once with a job and sends the text when it's done. */
export async function transcribe(audio: string, language?: string): Promise<Transcript> {
  let unlisten = () => {};
  type Result = { job: string; text?: string; error?: string; recording?: string; melody?: Transcript["melody"] };
  const done = new Promise<Result>((resolve) => {
    const results = new Map<string, Result>();
    let waitingFor: string | undefined;
    unlisten = listen("voiceTranscript", (result: Result) => {
      if (result.job === waitingFor) resolve(result);
      else results.set(result.job, result);
    });
    void engine.call<{ job: string }>("voice.transcribe", { audio, ...(language ? { language } : {}) }).then(({ job }) => {
      waitingFor = job;
      const early = results.get(job);
      if (early) resolve(early);
    }, (e) => resolve({ job: "", error: e instanceof Error ? e.message : String(e) }));
  });
  const result = await done.finally(unlisten);
  if (result.error) throw new Error(result.error);
  return { text: result.text ?? "", recording: result.recording, melody: result.melody };
}

/** A transcript as message text: what was said, then a note of the melody for the agent. */
export function transcriptText({ text, recording, melody }: Transcript) {
  if (!melody || !recording) return text.trim();
  const note = `[hummed melody: ${melody.notes} notes from ${melody.from.toFixed(1)} s in voice message ${recording}]`;
  return `${text.trim()} ${note}`.trim();
}

/** The voice shortcut was pressed: the agent's message box starts (or finishes) a recording when
    it's on screen, which the shortcut also sees to. */
export const useVoiceRequest = create<{ pending: boolean }>(() => ({ pending: false }));
