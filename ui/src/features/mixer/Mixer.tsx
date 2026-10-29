import type { Insert, Project } from "@groovekit/schema/project";
import { Plus } from "lucide-react";
import { useState } from "react";
import { Button } from "@/components/controls";
import { ContextMenu, ContextMenuContent, ContextMenuItem, ContextMenuSeparator, ContextMenuSub, ContextMenuTrigger } from "@/components/menu";
import { insertName, palette, uniqueId } from "@/lib/model";
import { useStable } from "@/lib/useStable";
import { execute, useEngine } from "@/state/engine";
import { setUi, useUi } from "@/state/ui";
import { InsertPanel } from "./InsertPanel";
import { Strip } from "./Strip";

/** The strips, and beside them the selected insert's effects and sends. */
export function Mixer() {
  const doc = useEngine((s) => s.doc)!;
  const selectedId = useUi((s) => s.selectedInsert);
  const selected = doc.mixer.inserts.find((i) => i.id === selectedId) ?? doc.mixer.inserts[0];
  const select = useStable((insert: string) => insert !== useUi.getState().selectedInsert && setUi({ selectedInsert: insert }));
  // The strip last right-clicked, for the one context menu all the strips share, and the one
  // whose name is being typed.
  const [menuInsert, setMenuInsert] = useState<string | null>(null);
  const [renaming, setRenaming] = useState<string | null>(null);
  const renamed = useStable(() => setRenaming(null));

  return (
    <div className="flex h-full">
      <ContextMenu>
      <ContextMenuTrigger asChild>
      <div className="flex min-w-0 flex-1 gap-px overflow-x-auto bg-line"
        onContextMenu={(e) => {
          const strip = (e.target as Element).closest<HTMLElement>("[data-insert]");
          if (strip) setMenuInsert(strip.dataset.insert!);
          else e.preventDefault();
        }}>
        {doc.mixer.inserts.map((insert, i) => (
          <Strip key={insert.id} insert={insert} index={i} selected={insert.id === selected?.id} onSelect={select}
            renaming={renaming === insert.id} onRenamed={renamed} />
        ))}
        <div className="flex w-24 shrink-0 items-start justify-center bg-panel pt-3">
          <Button size="sm" onClick={() => addInsert(doc)}><Plus size={12} /> Insert</Button>
        </div>
        <div className="flex-1 bg-panel" />
      </div>
      </ContextMenuTrigger>
      {menuInsert && <InsertMenu insertId={menuInsert} onRename={() => setRenaming(menuInsert)} />}
      </ContextMenu>
      {selected && (
        <div className="w-[290px] shrink-0 border-l border-line bg-panel">
          <InsertPanel doc={doc} insert={selected} index={doc.mixer.inserts.indexOf(selected)} />
        </div>
      )}
    </div>
  );
}

/** The menu for the insert last right-clicked (by id, so it follows the song's changes). */
function InsertMenu({ insertId, onRename }: { insertId: string; onRename: () => void }) {
  // Two selectors, each with a stable result: a selector making a new object every time loops.
  const index = useEngine((s) => s.doc?.mixer.inserts.findIndex((i) => i.id === insertId) ?? -1);
  const insert = useEngine((s) => s.doc?.mixer.inserts[index]);
  if (!insert) return null;
  const master = insert.id === "master";
  const update = (changes: Record<string, unknown>, label: string) => void execute({ type: "updateInsert", insert: insert.id, changes }, { label });

  return (
    <ContextMenuContent>
      {!master && <ContextMenuItem onSelect={onRename}>Rename</ContextMenuItem>}
      {!master && (
        <ContextMenuSub label="Colour">
          <div className="grid grid-cols-6 gap-1 p-1">
            {palette.map((c) => (
              <button key={c} type="button" className="h-5 w-5 rounded-[3px]" style={{ background: c }} onClick={() => update({ color: c }, "Colour insert")} />
            ))}
          </div>
        </ContextMenuSub>
      )}
      <ContextMenuItem onSelect={() => update({ mute: !insert.mute }, insert.mute ? "Unmute insert" : "Mute insert")}>{insert.mute ? "Unmute" : "Mute"}</ContextMenuItem>
      {!master && <ContextMenuItem onSelect={() => update({ solo: !insert.solo }, insert.solo ? "Unsolo insert" : "Solo insert")}>{insert.solo ? "Unsolo" : "Solo"}</ContextMenuItem>}
      <ContextMenuItem onSelect={() => update({ volumeDb: null, pan: null }, "Reset insert volume and pan")} disabled={!insert.volumeDb && !insert.pan}>
        Reset volume and pan
      </ContextMenuItem>
      <ContextMenuSeparator />
      <ContextMenuItem disabled={master} danger onSelect={() => void execute({ type: "removeInsert", insert: insert.id }, { label: `Remove ${insertName(insert, index)}` })}>
        Remove insert
      </ContextMenuItem>
    </ContextMenuContent>
  );
}

function addInsert(doc: Project) {
  const n = doc.mixer.inserts.length;
  const insert: Insert = { id: uniqueId(`insert-${n}`, doc.mixer.inserts), name: `Insert ${n}` };
  void execute({ type: "addInsert", insert }, { label: "Add insert" }).then(() => setUi({ selectedInsert: insert.id }));
}
