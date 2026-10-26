import { useEffect, useState } from "react";
import { Button, DragNumber } from "@/components/controls";
import { Dialog } from "@/components/dialog";
import { Dropdown, DropdownContent, DropdownItem, DropdownTrigger } from "@/components/menu";
import { inDesktopApp } from "@/engine/client";
import { pickFolder } from "@/lib/files";
import { call, engine, useEngine } from "@/state/engine";
import { setUi, useUi } from "@/state/ui";
import { ChevronDown } from "lucide-react";

interface AudioDevices {
  types: { type: string; outputs: string[]; inputs: string[] }[];
  current: {
    type: string; output: string; input: string; bluetoothInput?: boolean; sampleRate: number; bufferSize: number;
    /** Playing through macOS's default output (systemOutput), switching when it changes. */
    followSystemOutput?: boolean; systemOutput?: string;
  } | null;
}

interface MidiDevices {
  outputs: { name: string; enabled: boolean; clock: boolean }[];
  inputs: string[];
  enabledInputs?: string[];
}

function Section({ title, children }: { title: string; children: React.ReactNode }) {
  return (
    <div className="mb-4">
      <div className="mb-2 text-2xs uppercase tracking-wide text-faint">{title}</div>
      {children}
    </div>
  );
}

function Picker({ value, options, onPick, none }: { value: string; options: string[]; onPick: (v: string) => void; none?: boolean }) {
  return (
    <Dropdown>
      <DropdownTrigger asChild>
        <Button size="sm" className="w-full justify-between bg-control text-fg"><span className="truncate">{value || "None"}</span><ChevronDown size={11} /></Button>
      </DropdownTrigger>
      <DropdownContent>
        {none && <DropdownItem checked={value === ""} onSelect={() => onPick("")}>None</DropdownItem>}
        {options.map((o) => <DropdownItem key={o} checked={o === value} onSelect={() => onPick(o)}>{o}</DropdownItem>)}
      </DropdownContent>
    </Dropdown>
  );
}

export function SettingsDialog() {
  const open = useUi((s) => s.settingsOpen);
  const connected = useEngine((s) => s.status === "connected");
  const [audio, setAudio] = useState<AudioDevices>();
  const [midi, setMidi] = useState<MidiDevices>();
  const [plugins, setPlugins] = useState<{ running: boolean; scanned: number; total: number; plugins: number; current?: string }>();
  const [alignment, setAlignment] = useState<number>();

  useEffect(() => {
    if (!open || !connected) return;
    engine.call<AudioDevices>("audio.devices").then(setAudio, () => {});
    engine.call<MidiDevices>("midi.devices").then(setMidi, () => {});
    engine.call("plugins.status").then(setPlugins, () => {});
    engine.call<{ alignmentMs: number }>("record.alignment").then((r) => setAlignment(r.alignmentMs), () => {});
  }, [open, connected]);

  useEffect(() => {
    if (!open || !plugins?.running) return;
    const timer = setInterval(() => engine.call("plugins.status").then(setPlugins, () => {}), 1000);
    return () => clearInterval(timer);
  }, [open, plugins?.running]);

  const type = audio?.types.find((t) => t.type === audio.current?.type) ?? audio?.types[0];
  const setDevice = (params: Record<string, unknown>) => call<AudioDevices>("audio.setDevice", params).then(setAudio);

  return (
    <Dialog open={open} onOpenChange={(o) => setUi({ settingsOpen: o })} title="Settings">
      <Section title="Audio">
        {!audio ? <div className="text-xs text-faint">Loading…</div> : (
          <div className="grid grid-cols-[90px_1fr] items-center gap-x-3 gap-y-2 text-xs">
            <span className="text-dim">Driver</span>
            <Picker value={audio.current?.type ?? ""} options={audio.types.map((t) => t.type)} onPick={(v) => void setDevice({ type: v })} />
            <span className="text-dim">Output</span>
            {(() => {
              const system = `System default${audio.current?.systemOutput ? ` (${audio.current.systemOutput})` : ""}`;
              return (
                <Picker value={audio.current?.followSystemOutput ? system : audio.current?.output ?? ""} options={[system, ...(type?.outputs ?? [])]}
                  onPick={(v) => void setDevice({ output: v === system ? "" : v })} />
              );
            })()}
            <span className="text-dim">Input</span>
            <Picker value={audio.current?.input ?? ""} options={type?.inputs ?? []} onPick={(v) => void setDevice({ input: v })} none />
            {audio.current?.bluetoothInput && (
              <span className="col-start-2 text-2xs text-faint">Using this headset's microphone switches it to call quality for playback too.</span>
            )}
            <span className="text-dim">Buffer</span>
            <Picker value={`${audio.current?.bufferSize ?? ""} samples`} options={["64", "128", "256", "512", "1024", "2048"].map((b) => `${b} samples`)}
              onPick={(v) => void setDevice({ bufferSize: Number.parseInt(v) })} />
            <span className="text-dim">Sample rate</span>
            <span className="text-fg">{audio.current ? `${audio.current.sampleRate / 1000} kHz` : "—"}</span>
          </div>
        )}
      </Section>

      <LibrarySettings />

      <Section title="MIDI keyboards and controllers">
        {!midi ? <div className="text-xs text-faint">Loading…</div> : midi.inputs.length === 0 ? <div className="text-xs text-faint">No MIDI inputs. The typing keyboard in the top bar works without one.</div> : (
          <div className="flex flex-col gap-1">
            {midi.inputs.map((name) => (
              <label key={name} className="flex items-center gap-2 text-xs">
                <input type="checkbox" checked={midi.enabledInputs?.includes(name) ?? false} className="accent-[var(--color-accent)]"
                  onChange={(e) => void call<MidiDevices>("midi.setInput", { input: name, enabled: e.target.checked }).then(setMidi)} />
                <span className="text-fg">{name}</span>
                <span className="text-faint">plays the selected channel</span>
              </label>
            ))}
          </div>
        )}
      </Section>

      <Section title="MIDI clock">
        {!midi ? <div className="text-xs text-faint">Loading…</div> : midi.outputs.length === 0 ? <div className="text-xs text-faint">No MIDI outputs.</div> : (
          <div className="flex flex-col gap-1">
            {midi.outputs.map((o) => (
              <label key={o.name} className="flex items-center gap-2 text-xs">
                <input type="checkbox" checked={o.clock} className="accent-[var(--color-accent)]"
                  onChange={(e) => void call<MidiDevices>("midi.setClock", { output: o.name, enabled: e.target.checked }).then(setMidi)} />
                <span className="text-fg">{o.name}</span>
                <span className="text-faint">sends clock, start and stop</span>
              </label>
            ))}
          </div>
        )}
      </Section>

      <Section title="Recording">
        <div className="flex items-center gap-2 text-xs">
          <span className="text-dim">Alignment for this input device</span>
          {alignment !== undefined && (
            <DragNumber value={alignment} min={-500} max={500} step={0.5} format={(v) => `${v.toFixed(1)} ms`} className="text-fg"
              onCommit={(v) => void call<{ alignmentMs: number }>("record.alignment", { ms: v }).then((r) => setAlignment(r.alignmentMs))} />
          )}
        </div>
        <div className="mt-1 text-2xs text-faint">Use Calibrate in the record dialog with the hardware playing a clean kick to measure it.</div>
      </Section>

      <Section title="Plugins">
        <div className="flex items-center gap-2 text-xs">
          <span className="flex-1 text-dim">
            {plugins ? (plugins.running ? `Scanning ${plugins.scanned}/${plugins.total}… ${plugins.current ?? ""}` : `${plugins.plugins} plugins`) : "…"}
          </span>
          <Button size="sm" className="bg-control text-fg" disabled={plugins?.running} onClick={() => void call("plugins.scan").then(setPlugins)}>Scan for plugins</Button>
        </div>
      </Section>
    </Dialog>
  );
}

interface LibraryStatus {
  folders: string[];
  autoScan: boolean;
  progress?: { running: boolean; filesFound: number; analysed: number; unchanged: number; samples: number };
}

const home = (path: string) => path.replace(/^\/Users\/[^/]+/, "~");

function LibrarySettings() {
  const connected = useEngine((s) => s.status === "connected");
  const progress = useEngine((s) => s.libraryProgress);
  const [status, setStatus] = useState<LibraryStatus>();
  const [typing, setTyping] = useState(false);

  useEffect(() => {
    if (connected) engine.call<LibraryStatus>("library.status").then(setStatus, () => {});
  }, [connected, progress?.running]);

  const addFolder = async () => {
    if (!inDesktopApp) {
      setTyping(true);
      return;
    }
    const path = await pickFolder("Add a sample folder", status?.folders[0]);
    if (path) setStatus(await call<LibraryStatus>("library.addFolder", { path }));
  };

  const running = progress?.running ?? status?.progress?.running;

  return (
    <Section title="Sample library">
      {!status ? <div className="text-xs text-faint">Loading…</div> : (
        <div className="flex flex-col gap-2 text-xs">
          <div className="flex flex-col gap-1">
            {status.folders.length === 0 && <div className="text-faint">No folders yet.</div>}
            {status.folders.map((f) => (
              <div key={f} className="flex items-center gap-2 rounded-[4px] bg-bg px-2 py-1" title={f}>
                <span className="min-w-0 flex-1 truncate text-fg">{home(f)}</span>
                <Button size="sm" title="Remove from the library (the files stay)"
                  onClick={() => void call<LibraryStatus>("library.removeFolder", { path: f }).then(setStatus)}>Remove</Button>
              </div>
            ))}
            {typing && (
              <form className="flex gap-1" onSubmit={(e) => {
                e.preventDefault();
                const path = (new FormData(e.currentTarget).get("path") as string).trim();
                setTyping(false);
                if (path) void call<LibraryStatus>("library.addFolder", { path }).then(setStatus);
              }}>
                <input autoFocus name="path" placeholder="/Users/you/Samples" onKeyDown={(e) => e.stopPropagation()}
                  className="h-6 min-w-0 flex-1 rounded-[3px] bg-bg px-1.5 text-xs text-fg outline-none ring-1 ring-accent" />
                <Button size="sm" type="submit">Add</Button>
              </form>
            )}
          </div>
          <div className="flex items-center gap-2">
            <Button size="sm" className="bg-control text-fg" onClick={() => void addFolder()}>Add folder…</Button>
            <Button size="sm" className="bg-control text-fg" disabled={running} onClick={() => void call("library.scan")}>Scan now</Button>
            <span className="text-faint">
              {running && progress ? `Scanning ${progress.analysed + progress.unchanged} of ${progress.filesFound}…` : `${(status.progress?.samples ?? 0).toLocaleString()} samples`}
            </span>
          </div>
          <label className="flex items-center gap-2">
            <input type="checkbox" checked={status.autoScan} className="accent-[var(--color-accent)]"
              onChange={(e) => void call<LibraryStatus>("library.setAutoScan", { enabled: e.target.checked }).then(setStatus)} />
            <span className="text-fg">Look for new and changed samples when GrooveKit starts</span>
          </label>
          <div className="text-2xs text-faint">A scan of a big library takes a while and some memory. With this off, use Scan now after adding samples.</div>
        </div>
      )}
    </Section>
  );
}
