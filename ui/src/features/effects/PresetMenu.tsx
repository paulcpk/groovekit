import { ChevronDown, Trash2 } from "lucide-react";
import { useState } from "react";
import { Button } from "@/components/controls";
import { Dialog } from "@/components/dialog";
import { Dropdown, DropdownContent, DropdownItem, DropdownLabel, DropdownSeparator, DropdownTrigger } from "@/components/menu";
import { call, engine, toast } from "@/state/engine";

/** A preset: an effect's { params, mix }, or a channel's { filter, envelope }. */
export type Preset = { name: string } & Record<string, unknown>;

interface PresetList { factory: Preset[]; user: Preset[] }

/** The presets for an effect kind or for channels ("sampler"): GrooveKit's and the user's, and
    saving the current settings as one. */
export function PresetMenu({ kind, current, apply }: {
  kind: string; current: () => Omit<Preset, "name">; apply: (preset: Preset) => void;
}) {
  const [list, setList] = useState<PresetList>();
  const [saving, setSaving] = useState(false);
  const [name, setName] = useState("");

  const load = () => void engine.call<PresetList>("presets.list", { kind }).then(setList, () => {});

  const save = () => {
    const trimmed = name.trim();
    if (!trimmed) return;
    void call<PresetList>("presets.save", { kind, preset: { ...current(), name: trimmed } }).then((l) => {
      setList(l);
      setSaving(false);
      setName("");
      toast(`Saved the preset "${trimmed}"`);
    });
  };

  return (
    <>
      <Dropdown onOpenChange={(open) => open && load()}>
        <DropdownTrigger asChild>
          <button type="button" data-no-drag className="flex h-6 items-center gap-1 rounded-[4px] bg-control px-2 text-2xs text-dim hover:text-fg">
            Presets <ChevronDown size={10} />
          </button>
        </DropdownTrigger>
        <DropdownContent align="end" className="max-h-[60vh] overflow-y-auto">
          {!list ? <DropdownLabel>Loading…</DropdownLabel> : (
            <>
              <DropdownLabel>GrooveKit</DropdownLabel>
              {list.factory.map((p) => <DropdownItem key={p.name} onSelect={() => apply(p)}>{p.name}</DropdownItem>)}
              {list.user.length > 0 && (
                <>
                  <DropdownSeparator />
                  <DropdownLabel>Yours</DropdownLabel>
                  {list.user.map((p) => (
                    <DropdownItem key={p.name} onSelect={() => apply(p)}>
                      <span className="flex-1">{p.name}</span>
                      <span role="button" title="Delete this preset" className="ml-3 text-faint hover:text-rec"
                        onPointerDown={(e) => e.stopPropagation()}
                        onClick={(e) => {
                          e.stopPropagation();
                          e.preventDefault();
                          void call<PresetList>("presets.delete", { kind, name: p.name }).then(setList);
                        }}><Trash2 size={11} /></span>
                    </DropdownItem>
                  ))}
                </>
              )}
              <DropdownSeparator />
              <DropdownItem onSelect={() => setSaving(true)}>Save these settings as a preset…</DropdownItem>
            </>
          )}
        </DropdownContent>
      </Dropdown>
      <Dialog open={saving} onOpenChange={setSaving} title="Save as a preset" className="w-[360px]">
        <form className="flex flex-col gap-3" onSubmit={(e) => {
          e.preventDefault();
          save();
        }}>
          <input data-autofocus value={name} onChange={(e) => setName(e.target.value)} onKeyDown={(e) => e.stopPropagation()} placeholder="Name"
            className="h-7 rounded-[4px] bg-bg px-2 text-sm text-fg outline-none ring-1 ring-line focus:ring-accent" />
          <div className="text-2xs text-faint">Saved on this Mac, in GrooveKit's presets folder, for every project.</div>
          <div className="flex justify-end gap-2">
            <Button size="sm" type="button" onClick={() => setSaving(false)}>Cancel</Button>
            <Button size="sm" type="submit" disabled={!name.trim()} className="bg-accent text-black hover:bg-accent">Save</Button>
          </div>
        </form>
      </Dialog>
    </>
  );
}
