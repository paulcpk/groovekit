import { call, execute, toast, useEngine } from "@/state/engine";
import { setUi, useUi } from "@/state/ui";
import { formatDb } from "@/lib/model";

export function togglePlay() {
  const { playing } = useEngine.getState().transport;
  return call(playing ? "transport.stop" : "transport.play");
}

export function stop() {
  return call("transport.stop");
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
