import { useEffect, useState } from "react";
import { Button, DragNumber } from "@/components/controls";
import { Dialog } from "@/components/dialog";
import { Dropdown, DropdownContent, DropdownItem, DropdownTrigger } from "@/components/menu";
import { call, engine, useEngine } from "@/state/engine";
import { setUi, useUi } from "@/state/ui";
import { ChevronDown } from "lucide-react";

interface AudioDevices {
  types: { type: string; outputs: string[]; inputs: string[] }[];
  current: { type: string; output: string; input: string; sampleRate: number; bufferSize: number } | null;
}

interface MidiDevices {
  outputs: { name: string; enabled: boolean; clock: boolean }[];
  inputs: string[];
}

function Section({ title, children }: { title: string; children: React.ReactNode }) {
  return (
    <div className="mb-4">
      <div className="mb-2 text-2xs uppercase tracking-wide text-faint">{title}</div>
      {children}
    </div>
  );
}

function Picker({ value, options, onPick }: { value: string; options: string[]; onPick: (v: string) => void }) {
  return (
    <Dropdown>
      <DropdownTrigger asChild>
        <Button size="sm" className="w-full justify-between bg-control text-fg"><span className="truncate">{value || "None"}</span><ChevronDown size={11} /></Button>
      </DropdownTrigger>
      <DropdownContent>
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
            <Picker value={audio.current?.output ?? ""} options={type?.outputs ?? []} onPick={(v) => void setDevice({ output: v })} />
            <span className="text-dim">Input</span>
            <Picker value={audio.current?.input ?? ""} options={type?.inputs ?? []} onPick={(v) => void setDevice({ input: v })} />
            <span className="text-dim">Buffer</span>
            <Picker value={`${audio.current?.bufferSize ?? ""} samples`} options={["64", "128", "256", "512", "1024", "2048"].map((b) => `${b} samples`)}
              onPick={(v) => void setDevice({ bufferSize: Number.parseInt(v) })} />
            <span className="text-dim">Sample rate</span>
            <span className="text-fg">{audio.current ? `${audio.current.sampleRate / 1000} kHz` : "—"}</span>
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
