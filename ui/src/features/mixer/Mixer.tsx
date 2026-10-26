import type { Insert, Project } from "@groovekit/schema/project";
import { Plus } from "lucide-react";
import { useState } from "react";
import { Button } from "@/components/controls";
import { uniqueId } from "@/lib/model";
import { execute, useEngine } from "@/state/engine";
import { EffectEditor } from "./EffectEditor";
import { Strip } from "./Strip";

export function Mixer() {
  const doc = useEngine((s) => s.doc)!;
  const [effect, setEffect] = useState<{ insert: string; effect: string } | null>(null);
  const open = effect && doc.mixer.inserts.find((i) => i.id === effect.insert)?.effects?.find((e) => e.id === effect.effect) ? effect : null;

  return (
    <div className="flex h-full">
      <div className="flex min-w-0 flex-1 gap-px overflow-x-auto bg-line">
        {doc.mixer.inserts.map((insert, i) => (
          <Strip key={insert.id} doc={doc} insert={insert} index={i}
            openEffect={open?.insert === insert.id ? open.effect : undefined}
            onOpenEffect={(id) => setEffect(id ? { insert: insert.id, effect: id } : null)} />
        ))}
        <div className="flex w-24 shrink-0 items-start justify-center bg-panel pt-3">
          <Button size="sm" onClick={() => addInsert(doc)}><Plus size={12} /> Insert</Button>
        </div>
        <div className="flex-1 bg-panel" />
      </div>
      {open && (
        <div className="w-[300px] shrink-0 border-l border-line bg-panel">
          <EffectEditor insertId={open.insert} effectId={open.effect} onClose={() => setEffect(null)} />
        </div>
      )}
    </div>
  );
}

function addInsert(doc: Project) {
  const n = doc.mixer.inserts.length;
  const insert: Insert = { id: uniqueId(`insert-${n}`, doc.mixer.inserts), name: `Insert ${n}` };
  void execute({ type: "addInsert", insert }, { label: "Add insert" });
}
