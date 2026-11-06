// The command box (⌘K): a field in the middle of the screen, over every window, for a command or a
// request to the agent. A tap of ⌘K opens it to type in (and closes it again); holding ⌘K listens,
// and letting go runs what was said. Asked of the agent, the request goes to its chat, which comes
// to the front; when it's still busy, the request waits in the chat's message box.
//
// Plugins' windows are over the app's whole window, so they step aside while the box is up and come
// back after it, unless what ran brought something else to the front.

import { create } from "zustand";
import { send, useAgent, useApiKey, useDraft } from "@/features/agent/agent";
import { loadVoiceModel, transcribe, transcriptText, useRecorder, useVoiceModel, voiceRecorder } from "@/features/agent/voice";
import type { PluginInfo } from "@/features/rack/PluginPicker";
import type { Sample } from "@/features/browser/SampleRow";
import { stopPreview } from "@/features/shell/actions";
import { arrangePlugins, focusApp } from "@/features/windows/arrange";
import { engine, toast, useEngine } from "@/state/engine";
import { useWindows } from "@/state/windows";
import { itemsFor, showAgent, type Item, type Known, type ProjectEntry } from "./commands";

export type SpotlightMode = "typing" | "listening" | "transcribing";

interface Spotlight {
  open: boolean;
  mode: SpotlightMode;
  text: string;
  selected: number;
  /** The speech model isn't on this Mac yet: the box offers it. */
  needsModel: boolean;
  known: Known;
}

export const useSpotlight = create<Spotlight>(() => ({
  open: false, mode: "typing", text: "", selected: 0, needsModel: false, known: { instruments: [], projects: [], samples: [] },
}));

/** How long ⌘K is held before the box listens. */
const HOLD_MS = 300;

let held = false;
let holdTimer: ReturnType<typeof setTimeout> | undefined;
/** ⌘K came while the box was open: a tap closes it, holding listens. */
let wasOpen = false;
/** Plugins' windows that stepped aside, to come back. */
let aside: string[] = [];
let instruments: PluginInfo[] | undefined;

/** The loudest the microphone heard while listening (0..1, as the bars show it), and under what
    nothing counts as said. */
let loudest = 0;
const QUIET = 0.12;
useRecorder.subscribe((r) => {
  if (r.owner === "spotlight" && r.state === "recording") loudest = Math.max(loudest, r.levels[r.levels.length - 1] ?? 0);
});

export function spotlightItems(s: Pick<Spotlight, "text" | "known"> = useSpotlight.getState()): Item[] {
  return itemsFor(s.text, s.known);
}

/** ⌘K went down: opens the box (or keeps it), and listens if it's held. */
export function summonSpotlight() {
  wasOpen = useSpotlight.getState().open;
  held = true;
  clearTimeout(holdTimer);
  holdTimer = setTimeout(() => {
    if (held) void listen();
  }, HOLD_MS);
  if (!wasOpen) openSpotlight();
}

/** ⌘ was let go of: what was said runs; a tap on an open box closes it. */
export function releaseSpotlight() {
  if (!held) return;
  held = false;
  clearTimeout(holdTimer);
  const { mode } = useSpotlight.getState();
  if (mode === "listening") void finishListening(true);
  else if (wasOpen && mode === "typing") closeSpotlight();
}

export function spotlightHeld() {
  return held;
}

function openSpotlight() {
  const visible = useWindows.getState().plugins.filter((p) => p.visible).map((p) => p.key);
  aside = visible;
  if (visible.length) arrangePlugins(undefined, []);
  useSpotlight.setState({ open: true, mode: "typing", text: "", selected: 0, needsModel: false, known: { ...useSpotlight.getState().known, samples: [] } });
  void focusApp();
  void refreshKnown();
}

/** The instruments (once) and the recent songs (each time), for the list. */
async function refreshKnown() {
  if (useEngine.getState().status !== "connected") return;
  const [plugins, recent] = await Promise.all([
    instruments ? Promise.resolve(instruments) : engine.call<{ plugins: PluginInfo[] }>("plugins.list", { instrument: true }).then((r) => r.plugins, () => []),
    engine.call<{ projects: ProjectEntry[] }>("project.recent").then((r) => r.projects, () => []),
  ]);
  instruments = plugins;
  useSpotlight.setState((s) => ({ known: { ...s.known, instruments: plugins, projects: recent } }));
}

let searchTimer: ReturnType<typeof setTimeout> | undefined;

export function setSpotlightText(text: string) {
  useSpotlight.setState({ text, selected: 0 });
  // Samples by name, once the typing pauses.
  clearTimeout(searchTimer);
  const words = text.trim();
  if (words.length < 3 || useEngine.getState().status !== "connected") {
    useSpotlight.setState((s) => ({ known: { ...s.known, samples: [] } }));
    return;
  }
  searchTimer = setTimeout(() => {
    void engine.call<{ results: Sample[] }>("library.search", { query: words, limit: 6 }).then((r) => {
      if (useSpotlight.getState().text.trim() === words) useSpotlight.setState((s) => ({ known: { ...s.known, samples: r.results } }));
    }, () => {});
  }, 200);
}

export function moveSpotlightSelection(delta: number) {
  const items = spotlightItems();
  if (!items.length) return;
  const selected = (useSpotlight.getState().selected + delta + items.length) % items.length;
  useSpotlight.setState({ selected });
  const sample = items[selected].sample;
  if (sample) void previewSample(sample);
}

function previewSample(sample: Sample) {
  return engine.call("preview.play", { path: sample.path }).then(() => useEngine.setState({ previewing: sample.path }), () => {});
}

/** Closes the box; the plugins' windows that stepped aside come back unless told otherwise. */
export function closeSpotlight({ restore = true } = {}) {
  if (!useSpotlight.getState().open) return;
  held = false;
  clearTimeout(holdTimer);
  clearTimeout(searchTimer);
  const recorder = useRecorder.getState();
  if (recorder.owner === "spotlight" && recorder.state !== "idle") voiceRecorder.cancel();
  if (useEngine.getState().previewing) void stopPreview();
  useSpotlight.setState({ open: false, mode: "typing", text: "", needsModel: false });
  if (restore && aside.length) arrangePlugins(undefined, aside);
  aside = [];
}

/** Runs an item of the list (the one chosen if none is given). */
export function runSpotlight(item?: Item) {
  const state = useSpotlight.getState();
  const items = spotlightItems(state);
  const chosen = item ?? items[Math.min(state.selected, items.length - 1)];
  if (!chosen) return;
  if (chosen.kind === "agent") {
    askAgent(state.text.trim());
    return;
  }
  closeSpotlight({ restore: !!chosen.keepsWindows });
  chosen.run();
}

/** Passes words to the agent: sent in its chat, which comes to the front. */
export function askAgent(text: string) {
  if (!text) return;
  closeSpotlight({ restore: false });
  showAgent("chat");
  if (!useApiKey.getState().key) {
    useDraft.setState({ text });
    toast("The agent needs your API key first: it's asked for in the chat.");
  } else if (useAgent.getState().running || useEngine.getState().status !== "connected") {
    useDraft.setState({ text });
    toast("The agent is still busy: your request waits in the message box.");
  } else {
    void send(text);
  }
}

/** Starts listening (⌘K held, or the microphone clicked). */
export async function listen() {
  const recorder = voiceRecorder;
  if (useRecorder.getState().state !== "idle") return;
  const model = useVoiceModel.getState() ?? (await loadVoiceModel().catch(() => null));
  if (!model?.ready) {
    useSpotlight.setState({ needsModel: true });
    return;
  }
  void loadVoiceModel(true).catch(() => {});
  useSpotlight.setState({ mode: "listening", needsModel: false });
  loudest = 0;
  await recorder.start("spotlight");
  if (useRecorder.getState().state === "idle") useSpotlight.setState({ mode: "typing" });
}

/** Stops listening and turns what was said into text: run straight away, or left in the field. */
export async function finishListening(run: boolean) {
  const recorder = voiceRecorder;
  if (useRecorder.getState().owner !== "spotlight") return;
  if (useRecorder.getState().state === "starting") {
    recorder.cancel();
    useSpotlight.setState({ mode: "typing" });
    return;
  }
  const audio = await recorder.stop();
  // Nothing said (⌘ only held on to): the speech model would make words up from the quiet.
  if (!audio || loudest < QUIET) {
    useSpotlight.setState({ mode: "typing" });
    return;
  }
  useSpotlight.setState({ mode: "transcribing" });
  try {
    const text = transcriptText(await transcribe(audio));
    if (!useSpotlight.getState().open) return;
    if (!text) {
      useSpotlight.setState({ mode: "typing" });
      toast("Didn't catch that. Try again a little closer to the microphone.");
      return;
    }
    useSpotlight.setState({ mode: "typing" });
    setSpotlightText(text);
    if (run) runSpotlight();
  } catch (e) {
    useSpotlight.setState({ mode: "typing" });
    toast(e instanceof Error ? e.message : String(e), "error");
  }
}

/** The box's keys, heard before anything else: ⌘K from anywhere (a text field too), and Escape. */
export function spotlightKeyDown(e: KeyboardEvent) {
  const cmdK = e.metaKey && !e.shiftKey && !e.altKey && !e.ctrlKey && (e.key.toLowerCase() === "k" || e.code === "KeyK");
  if (cmdK) {
    e.preventDefault();
    e.stopImmediatePropagation();
    if (!e.repeat) summonSpotlight();
    return;
  }
  if (!useSpotlight.getState().open) return;
  if (e.key === "Escape") {
    e.preventDefault();
    e.stopImmediatePropagation();
    if (useSpotlight.getState().mode === "listening") {
      voiceRecorder.cancel();
      useSpotlight.setState({ mode: "typing" });
    } else closeSpotlight();
  }
}

export function spotlightKeyUp(e: KeyboardEvent) {
  if (held && (e.key === "Meta" || e.key.toLowerCase() === "k")) releaseSpotlight();
}

/** If ⌘ was let go of where the app couldn't see it, the next move of the mouse tells. */
export function spotlightPointerMove(e: PointerEvent) {
  if (held && !e.metaKey) releaseSpotlight();
}
