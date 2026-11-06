// What the command box (⌘K) can do with what's typed or said: GrooveKit's own commands (play,
// tempo 128, the mixer), going to an open window, a new channel with an instrument, a recent song,
// a sample from the library, or asking the agent. A command runs straight away only when the words
// name it exactly; anything else goes to the agent first, the other matches listed under it.

import type { LucideIcon } from "lucide-react";
import {
  AppWindow, AudioWaveform, BookOpen, Gauge, Circle, FilePlus, FileAudio, FolderOpen, KeyboardMusic, LayoutDashboard, LayoutGrid, Map as MapIcon,
  MessageSquare, Piano, Play, Redo2, Repeat, Save, Settings, SkipBack, SlidersHorizontal, Sparkles, Square, StickyNote, Undo2, Upload,
} from "lucide-react";

import { addInstrument, BUILT_IN, instrumentName, type InstrumentChoice } from "@/features/rack/instruments";
import type { PluginInfo } from "@/features/rack/PluginPicker";
import type { Sample } from "@/features/browser/SampleRow";
import { addChannel } from "@/features/browser/SampleRow";
import { useAgentView, type AgentView } from "@/features/agent/maps";
import { redo, setPosition, setTempo, stop, togglePlay, toggleLoop, undo } from "@/features/shell/actions";
import { browseForProject, openProject, saveProject } from "@/features/shell/ProjectDialog";
import { bringToFront, openTargets } from "@/features/windows/arrange";
import { targetIcon, targetLabel } from "@/features/windows/labels";
import { toggleOverview } from "@/features/windows/overviewState";
import { fileName } from "@/lib/model";
import { useEngine } from "@/state/engine";
import { setUi, type BottomPanel } from "@/state/ui";

export type ItemKind = "agent" | "command" | "window" | "instrument" | "project" | "sample";

export interface Item {
  id: string;
  kind: ItemKind;
  label: string;
  /** Said in grey after the label: where it comes from, what it'll do. */
  detail?: string;
  icon: LucideIcon;
  run: () => void;
  /** For a sample: heard when chosen with the arrow keys. */
  sample?: Sample;
  /** Whether plugin windows that stepped aside for the box come back after it ran. */
  keepsWindows?: boolean;
}

export interface ProjectEntry { path: string; name: string; modified: number }

/** What the box knows besides the words: fetched as it opens. */
export interface Known {
  instruments: PluginInfo[];
  projects: ProjectEntry[];
  samples: Sample[];
}

/** Lower case, no punctuation, single spaces: "Play." and "play" are the same. */
export function normalise(text: string) {
  return text.toLowerCase().replace(/[^\p{L}\p{N}\s#.-]/gu, " ").replace(/(?<!\d)\.|\.(?!\d)/g, " ").replace(/\s+/g, " ").trim();
}

/** The words with "open", "show", "go to" and the like taken off the front. */
const VERBS = /^(?:please |)(?:open|show|show me|go to|switch to|bring up|view|add|new|add a|add an|add a new|load|start|the)\s+/;
function core(words: string) {
  let rest = words;
  for (let previous = ""; previous !== rest;) {
    previous = rest;
    rest = rest.replace(VERBS, "");
  }
  return rest;
}

function showPanel(panel: BottomPanel) {
  setUi({ bottomOpen: true, bottomPanel: panel });
}

/** The agent's panel on the given tab, over everything else. */
export function showAgent(view: AgentView = "chat") {
  setUi({ sidebarOpen: true, sidebarTab: "activity" });
  useAgentView.setState({ view });
  bringToFront("main");
}

interface Command {
  label: string;
  /** Other ways of saying it; the label counts too. */
  names: string[];
  icon: LucideIcon;
  run: () => void;
  keepsWindows?: boolean;
}

const COMMANDS: Command[] = [
  { label: "Play or pause", names: ["play", "pause", "start playing", "play the song"], icon: Play, run: () => void togglePlay(), keepsWindows: true },
  { label: "Stop", names: ["stop", "stop playing"], icon: Square, run: () => void stop(), keepsWindows: true },
  { label: "Back to the start", names: ["back to the start", "to the start", "rewind", "go to the start"], icon: SkipBack, run: () => void setPosition(0), keepsWindows: true },
  { label: "Loop on or off", names: ["loop", "toggle loop"], icon: Repeat, run: () => void toggleLoop(), keepsWindows: true },
  { label: "Undo", names: ["undo", "undo that"], icon: Undo2, run: () => void undo(), keepsWindows: true },
  { label: "Redo", names: ["redo"], icon: Redo2, run: () => void redo(), keepsWindows: true },
  { label: "Save", names: ["save", "save the song", "save song"], icon: Save, run: () => void saveProject(), keepsWindows: true },
  { label: "Playlist", names: ["playlist", "arrangement", "the arrangement"], icon: LayoutDashboard, run: () => { setUi({ bottomOpen: false }); bringToFront("main"); } },
  { label: "Channel rack", names: ["channel rack", "rack", "channels"], icon: LayoutGrid, run: () => { showPanel("rack"); bringToFront("main"); } },
  { label: "Piano roll", names: ["piano roll", "piano", "notes"], icon: Piano, run: () => { showPanel("piano"); bringToFront("main"); } },
  { label: "Mixer", names: ["mixer", "the mixer", "mix"], icon: SlidersHorizontal, run: () => { showPanel("mixer"); bringToFront("main"); } },
  { label: "Browser", names: ["browser", "samples", "sample library", "library"], icon: FileAudio, run: () => { setUi({ sidebarOpen: true, sidebarTab: "browser" }); bringToFront("main"); } },
  { label: "Agent", names: ["agent", "chat", "the agent"], icon: MessageSquare, run: () => showAgent("chat") },
  { label: "Agent's notes", names: ["agent notes", "notes for the agent"], icon: StickyNote, run: () => showAgent("notes") },
  { label: "Styles", names: ["styles", "style guide", "style guides"], icon: BookOpen, run: () => showAgent("styles") },
  { label: "Song maps", names: ["maps", "song maps", "references", "reference tracks"], icon: MapIcon, run: () => showAgent("maps") },
  { label: "All open windows", names: ["windows", "overview", "all windows", "open windows"], icon: AppWindow, run: () => void toggleOverview() },
  { label: "Record", names: ["record", "recording"], icon: Circle, run: () => setUi({ recordOpen: true }) },
  { label: "Export", names: ["export", "render", "bounce", "export the song"], icon: Upload, run: () => setUi({ exportOpen: true }) },
  { label: "New song", names: ["new song", "new project"], icon: FilePlus, run: () => setUi({ projectDialog: "new" }) },
  { label: "Open a song…", names: ["open song", "open project", "open a song", "open a project"], icon: FolderOpen, run: () => void browseForProject() },
  { label: "Settings", names: ["settings", "audio settings", "preferences"], icon: Settings, run: () => setUi({ settingsOpen: true }) },
];

/** "tempo 128", "bpm 128", "128 bpm", "set the tempo to 128". */
function tempoOf(words: string) {
  const m = words.match(/^(?:set (?:the )?)?(?:tempo|bpm)(?: to| at)? (\d{2,3}(?:\.\d+)?)(?: bpm)?$/) ?? words.match(/^(\d{2,3}(?:\.\d+)?) ?bpm$/);
  const tempo = m ? Number(m[1]) : NaN;
  return tempo >= 20 && tempo <= 999 ? tempo : undefined;
}

/** Whether every word typed starts a word of the name ("pia ro" finds "Piano roll"). */
function startsWords(name: string, words: string) {
  const parts = normalise(name).split(" ");
  return words.split(" ").every((w) => parts.some((p) => p.startsWith(w)));
}

function instrumentChoices(instruments: PluginInfo[]): InstrumentChoice[] {
  return [...BUILT_IN, ...instruments.map((plugin) => ({ kind: "plugin" as const, plugin }))];
}

/** The box's list for the words: what they name exactly first, else the agent, then what they
    partly match. Without words, the commands, to show what there is. */
export function itemsFor(text: string, known: Known): Item[] {
  const words = normalise(text);
  const doc = useEngine.getState().doc;
  if (!words) return COMMANDS.slice(0, 8).map((c) => commandItem(c));

  const exact: Item[] = [];
  const partly: Item[] = [];
  const named = core(words);

  const tempo = tempoOf(words);
  if (tempo) exact.push({ id: "tempo", kind: "command", label: `Tempo ${tempo}`, detail: "the song’s tempo", icon: Gauge, run: () => void setTempo(tempo), keepsWindows: true });

  for (const c of COMMANDS) {
    const names = [c.label, ...c.names].map(normalise);
    if (names.includes(words) || names.includes(named)) exact.push(commandItem(c));
    else if (words.length >= 2 && names.some((n) => startsWords(n, words))) partly.push(commandItem(c));
  }

  for (const target of openTargets()) {
    if (target === "main") continue;
    const label = targetLabel(target, doc);
    const item: Item = { id: `window:${target}`, kind: "window", label, detail: "open window", icon: targetIcon(target), run: () => bringToFront(target) };
    if (normalise(label) === named) exact.push(item);
    else if (words.length >= 2 && startsWords(label, named || words)) partly.push(item);
  }

  if (doc) {
    for (const choice of instrumentChoices(known.instruments)) {
      const name = instrumentName(choice);
      const item: Item = {
        id: `instrument:${choice.kind === "plugin" ? choice.plugin.uid : choice.kind}`, kind: "instrument", label: `Add ${name}`,
        detail: choice.kind === "plugin" ? `a new channel · ${choice.plugin.manufacturer}` : "a new channel", icon: KeyboardMusic,
        run: () => void addInstrument(doc, choice), keepsWindows: true,
      };
      if (normalise(name) === named) exact.push(item);
      else if (named.length >= 2 && startsWords(name, named)) partly.push(item);
    }
  }

  const current = useEngine.getState().project;
  for (const p of known.projects) {
    if (p.path === current) continue;
    const item: Item = { id: `project:${p.path}`, kind: "project", label: `Open ${p.name}`, detail: "song", icon: FolderOpen, run: () => void openProject(p.path) };
    if (normalise(p.name) === named) exact.push(item);
    else if (named.length >= 2 && startsWords(p.name, named)) partly.push(item);
  }

  const agent: Item = { id: "agent", kind: "agent", label: text.trim(), detail: "ask the agent", icon: Sparkles, run: () => {} };
  const samples = known.samples.map<Item>((s) => ({
    id: `sample:${s.path}`, kind: "sample", label: fileName(s.path), detail: `${s.loop ? "loop" : "one-shot"} · ${s.category} · a new channel`,
    icon: s.loop ? AudioWaveform : FileAudio, sample: s, run: () => addChannel(s), keepsWindows: true,
  }));

  return [...exact.slice(0, 1), agent, ...exact.slice(1), ...partly.slice(0, 6), ...samples];
}

function commandItem(c: Command): Item {
  return { id: `command:${c.label}`, kind: "command", label: c.label, icon: c.icon, run: c.run, keepsWindows: c.keepsWindows };
}
