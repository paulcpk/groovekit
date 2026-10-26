import { call, engine, execute, toast, useEngine } from "@/state/engine";
import { setUi, useUi } from "@/state/ui";
import { formatDb } from "@/lib/model";

/** Auditions a sample from the browser, or stops it if it's the one playing. */
export function previewSample(path: string, durationSeconds: number) {
  if (useEngine.getState().previewing === path) return stopPreview();
  useEngine.setState({ previewing: path });
  setTimeout(() => useEngine.getState().previewing === path && useEngine.setState({ previewing: undefined }), Math.min(30, durationSeconds) * 1000);
  return call("preview.play", { path });
}

export function stopPreview() {
  if (!useEngine.getState().previewing) return Promise.resolve();
  useEngine.setState({ previewing: undefined });
  return call("preview.stop");
}

/** Play, or pause where it is. A sample being auditioned stops first. */
export function togglePlay() {
  if (useEngine.getState().previewing) return stopPreview();
  const { playing } = useEngine.getState().transport;
  return call(playing ? "transport.stop" : "transport.play");
}

/** Stops and goes back to the start. */
export async function stop() {
  await stopPreview();
  await call("transport.stop");
  await setPosition(0);
}

/** Song plays the playlist; pattern loops the pattern selected in the channel rack. */
export function setPlayMode(mode: "song" | "pattern") {
  if (mode === "song") return call("transport.setMode", { mode });
  const doc = useEngine.getState().doc;
  const pattern = useUi.getState().selectedPattern ?? doc?.patterns[0]?.id;
  if (!pattern) {
    toast("Make a pattern in the channel rack first");
    return Promise.resolve();
  }
  return call("transport.setMode", { mode, pattern });
}

export function setPosition(position: number) {
  return call("transport.setPosition", { position: Math.max(0, Math.round(position)) });
}

export function undo() {
  return call("undo");
}

export function redo() {
  return call("redo");
}

export async function setLoop(range: { start: number; end: number } | undefined, looping: boolean) {
  setUi({ loop: range, looping });
  await call("transport.setLoop", looping && range ? range : {});
}

export function toggleLoop() {
  const { loop, looping } = useUi.getState();
  if (!loop) {
    toast("Drag on the top half of the ruler to set a loop region");
    return;
  }
  return setLoop(loop, !looping);
}

export async function renderSong() {
  toast("Rendering…");
  const result = await call("render", {});
  toast(`Rendered to ${result.path} · ${result.analysis.integratedLufs.toFixed(1)} LUFS, true peak ${formatDb(result.analysis.truePeakDb)} dB`);
}

export async function masterSong(targetLufs = -14) {
  toast(`Mastering to ${targetLufs} LUFS…`);
  const r = await call("master", { targetLufs });
  toast(`Master limiter at ${formatDb(r.inputGainDb)} dB: ${r.integratedLufs.toFixed(1)} LUFS, true peak ${formatDb(r.truePeakDb)} dB${r.reachedTarget ? "" : " (target not reached)"}`);
}

export function setTempo(tempo: number) {
  return execute({ type: "setTempo", tempo: Math.round(tempo * 100) / 100 });
}

export function setMetronome(enabled: boolean) {
  return call("transport.setMetronome", { enabled });
}

export function setCountIn(bars: number) {
  return call("transport.setMetronome", { countInBars: bars });
}

/** In pattern mode, records notes from the keyboards into the selected pattern on the selected
    channel; in song mode, picks audio inputs to record. Stops a recording in progress. */
export function record() {
  const { transport, doc } = useEngine.getState();
  if (transport.recording) return call("record.stop");
  if (transport.mode !== "pattern") {
    setUi({ recordOpen: true });
    return Promise.resolve();
  }
  const { selectedPattern, selectedChannel, recordQuantize } = useUi.getState();
  const pattern = selectedPattern ?? doc?.patterns[0]?.id;
  const channel = selectedChannel ?? doc?.channels[0]?.id;
  if (!pattern || !channel) {
    toast("Pick a pattern and a channel to record notes into");
    return Promise.resolve();
  }
  return call("record.notes", { pattern, channel, quantize: recordQuantize });
}

/** Plays a note on a channel briefly, to hear it while editing. */
export function auditionNote(channel: string, key: number, velocity = 100, ms = 220) {
  void engine.call("live.noteOn", { channel, key, velocity }).catch(() => {});
  setTimeout(() => void engine.call("live.noteOff", { key }).catch(() => {}), ms);
}
