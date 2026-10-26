import { useState } from "react";
import { ChevronDown, Circle, Layers, ListMusic, Piano, Play, Redo2, Repeat, Settings, SlidersVertical, Square, Undo2, PanelLeft } from "lucide-react";
import { Button, DragNumber, IconButton } from "@/components/controls";
import { Dropdown, DropdownContent, DropdownItem, DropdownLabel, DropdownSeparator, DropdownTrigger } from "@/components/menu";
import { cn } from "@/lib/cn";
import { formatPosition, formatTime } from "@/lib/model";
import { inDesktopApp } from "@/engine/client";
import { execute, useEngine } from "@/state/engine";
import { setUi, useUi, type BottomPanel } from "@/state/ui";
import { masterSong, redo, renderSong, setTempo, toggleLoop, togglePlay, undo } from "./actions";
import { browseForProject, openProject, recentProjects, type RecentProject } from "./ProjectDialog";
import { ProjectName } from "./ProjectName";

export const snaps: { label: string; ticks: number }[] = [
  { label: "Bar", ticks: 0 }, // resolved against the time signature
  { label: "Beat", ticks: 960 },
  { label: "1/8", ticks: 480 },
  { label: "1/16", ticks: 240 },
  { label: "1/32", ticks: 120 },
  { label: "Off", ticks: 1 },
];

function Position() {
  const doc = useEngine((s) => s.doc);
  const t = useEngine((s) => s.transport);
  return (
    <div className="flex h-7 items-center gap-2 rounded-[4px] bg-bg px-2.5 font-mono text-sm tabular">
      <span className="min-w-[56px] text-fg">{formatPosition(t.position, doc)}</span>
      <span className="text-faint">{formatTime(t.seconds)}</span>
    </div>
  );
}

function TransportButtons() {
  const { playing, recording } = useEngine((s) => s.transport);
  const looping = useUi((s) => s.looping);

  return (
    <div className="flex items-center gap-0.5">
      <IconButton title="Play / stop (Space)" active={playing} tone="accent" onClick={() => void togglePlay()}>
        {playing ? <Square size={13} fill="currentColor" /> : <Play size={14} fill="currentColor" />}
      </IconButton>
      <IconButton title="Record…" active={recording} tone="rec" onClick={() => setUi({ recordOpen: true })}>
        <Circle size={12} fill={recording ? "currentColor" : "none"} className={cn(!recording && "text-rec")} />
      </IconButton>
      <IconButton title="Loop (L)" active={looping} tone="accent" onClick={() => void toggleLoop()}>
        <Repeat size={14} />
      </IconButton>
    </div>
  );
}

function Tempo() {
  const doc = useEngine((s) => s.doc);
  if (!doc) return null;
  const ts = doc.timeSignature ?? {};

  return (
    <div className="flex items-center gap-3 text-sm">
      <div className="flex items-baseline gap-1" title="Tempo: drag or double-click to type">
        <DragNumber value={doc.tempo} min={20} max={999} step={1} format={(v) => v.toFixed(v % 1 ? 2 : 0)} onCommit={(v) => void setTempo(v)} className="text-fg" />
        <span className="text-2xs text-faint">BPM</span>
      </div>
      <div className="flex items-baseline text-fg" title="Time signature">
        <DragNumber value={ts.numerator ?? 4} min={1} max={32} pixelsPerStep={8}
          onCommit={(v) => void execute({ type: "setTimeSignature", timeSignature: { numerator: v, denominator: ts.denominator ?? 4 } })} />
        <span className="text-faint">/</span>
        <span>{ts.denominator ?? 4}</span>
      </div>
    </div>
  );
}

function SnapMenu() {
  const snap = useUi((s) => s.snap);
  const current = snaps.find((s) => s.ticks === snap) ?? snaps[0];
  return (
    <Dropdown>
      <DropdownTrigger asChild>
        <Button size="sm" title="Snap">
          <span className="text-faint">Snap</span> {current.label} <ChevronDown size={11} />
        </Button>
      </DropdownTrigger>
      <DropdownContent>
        {snaps.map((s) => (
          <DropdownItem key={s.label} checked={s.ticks === snap} onSelect={() => setUi({ snap: s.ticks })}>{s.label}</DropdownItem>
        ))}
      </DropdownContent>
    </Dropdown>
  );
}

function PanelToggles() {
  const { bottomPanel, bottomOpen, sidebarOpen } = useUi();
  const show = (panel: BottomPanel) => setUi(bottomOpen && bottomPanel === panel ? { bottomOpen: false } : { bottomOpen: true, bottomPanel: panel });

  return (
    <div className="flex items-center gap-0.5">
      <IconButton title="Browser and history" active={sidebarOpen} onClick={() => setUi({ sidebarOpen: !sidebarOpen })}><PanelLeft size={15} /></IconButton>
      <IconButton title="Channel rack (F6)" active={bottomOpen && bottomPanel === "rack"} onClick={() => show("rack")}><Layers size={15} /></IconButton>
      <IconButton title="Piano roll (F7)" active={bottomOpen && bottomPanel === "piano"} onClick={() => show("piano")}><Piano size={15} /></IconButton>
      <IconButton title="Mixer (F9)" active={bottomOpen && bottomPanel === "mixer"} onClick={() => show("mixer")}><SlidersVertical size={15} /></IconButton>
    </div>
  );
}

function ProjectMenu() {
  const current = useEngine((s) => s.project);
  const [recent, setRecent] = useState<RecentProject[]>([]);

  return (
    <Dropdown onOpenChange={(open) => open && void recentProjects().then(setRecent, () => {})}>
      <DropdownTrigger asChild>
        <IconButton size="sm" title="Projects"><ChevronDown size={12} /></IconButton>
      </DropdownTrigger>
      <DropdownContent>
        <DropdownItem onSelect={() => setUi({ projectDialog: "new" })}>New project… <span className="ml-auto text-2xs text-faint">⌘N</span></DropdownItem>
        <DropdownItem onSelect={() => void browseForProject()}>Open project… <span className="ml-auto text-2xs text-faint">⌘O</span></DropdownItem>
        {recent.some((p) => p.path !== current) && (
          <>
            <DropdownSeparator />
            <DropdownLabel>Recent</DropdownLabel>
            {recent.filter((p) => p.path !== current).slice(0, 8).map((p) => (
              <DropdownItem key={p.path} onSelect={() => void openProject(p.path)}>
                <span className="truncate" title={p.path}>{p.name}</span>
              </DropdownItem>
            ))}
          </>
        )}
      </DropdownContent>
    </Dropdown>
  );
}

function SongMenu() {
  return (
    <Dropdown>
      <DropdownTrigger asChild>
        <Button size="sm"><ListMusic size={13} /> Song <ChevronDown size={11} /></Button>
      </DropdownTrigger>
      <DropdownContent align="end">
        <DropdownLabel>Bounce</DropdownLabel>
        <DropdownItem onSelect={() => void renderSong()}>Render to WAV</DropdownItem>
        <DropdownSeparator />
        <DropdownLabel>Master limiter</DropdownLabel>
        <DropdownItem onSelect={() => void masterSong(-14)}>Master to −14 LUFS (streaming)</DropdownItem>
        <DropdownItem onSelect={() => void masterSong(-9)}>Master to −9 LUFS (club)</DropdownItem>
      </DropdownContent>
    </Dropdown>
  );
}

export function TopBar() {
  const status = useEngine((s) => s.status);
  const head = useEngine((s) => s.head);

  return (
    // In the desktop app the bar is also the title bar: it moves the window, and leaves room
    // for the window buttons.
    <div className={cn("flex h-11 shrink-0 items-center gap-3 bg-panel px-3", inDesktopApp && "pl-[84px]")} data-tauri-drag-region>
      <div className="flex items-center gap-2" data-tauri-drag-region>
        <div className="text-[13px] font-semibold tracking-tight text-fg">GrooveKit</div>
        <ProjectName />
        <ProjectMenu />
      </div>
      <div className="h-5 w-px bg-line" />
      <TransportButtons />
      <Position />
      <Tempo />
      <SnapMenu />
      <div className="flex-1" />
      <div className="flex items-center gap-0.5">
        <IconButton title="Undo (⌘Z)" disabled={head === null} onClick={() => void undo()}><Undo2 size={15} /></IconButton>
        <IconButton title="Redo (⇧⌘Z)" onClick={() => void redo()}><Redo2 size={15} /></IconButton>
      </div>
      <SongMenu />
      <PanelToggles />
      <IconButton title="Audio and MIDI settings" onClick={() => setUi({ settingsOpen: true })}><Settings size={15} /></IconButton>
      <div
        title={status === "connected" ? "Connected to the engine" : status}
        className={cn("h-2 w-2 rounded-full", status === "connected" ? "bg-ok" : status === "connecting" ? "bg-accent" : "bg-rec")}
      />
    </div>
  );
}
