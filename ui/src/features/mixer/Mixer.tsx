import type { Insert, Project } from "@groovekit/schema/project";
import { Plus } from "lucide-react";
import { Button } from "@/components/controls";
import { uniqueId } from "@/lib/model";
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

  return (
    <div className="flex h-full">
      <div className="flex min-w-0 flex-1 gap-px overflow-x-auto bg-line">
        {doc.mixer.inserts.map((insert, i) => (
          <Strip key={insert.id} insert={insert} index={i} selected={insert.id === selected?.id} onSelect={select} />
        ))}
        <div className="flex w-24 shrink-0 items-start justify-center bg-panel pt-3">
          <Button size="sm" onClick={() => addInsert(doc)}><Plus size={12} /> Insert</Button>
        </div>
        <div className="flex-1 bg-panel" />
      </div>
      {selected && (
        <div className="w-[290px] shrink-0 border-l border-line bg-panel">
          <InsertPanel doc={doc} insert={selected} index={doc.mixer.inserts.indexOf(selected)} />
        </div>
      )}
    </div>
  );
}

function addInsert(doc: Project) {
  const n = doc.mixer.inserts.length;
  const insert: Insert = { id: uniqueId(`insert-${n}`, doc.mixer.inserts), name: `Insert ${n}` };
  void execute({ type: "addInsert", insert }, { label: "Add insert" }).then(() => setUi({ selectedInsert: insert.id }));
}
