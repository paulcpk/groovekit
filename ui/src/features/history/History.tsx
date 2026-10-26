import { Bot, ChevronRight, Monitor, Undo2 } from "lucide-react";
import { useEffect, useMemo, useState } from "react";
import { Button, Empty } from "@/components/controls";
import { cn } from "@/lib/cn";
import { call, engine, useEngine } from "@/state/engine";

interface Entry {
  id: number;
  parent?: number;
  time: number;
  origin: string;
  group?: string;
  label: string;
}

interface Row {
  key: string;
  entries: Entry[];
  origin: string;
  group?: string;
}

function timeOf(t: number) {
  return new Date(t).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" });
}

/** Every change, newest first. Runs of one request (a group) fold into one row; undone changes are dimmed. */
export function History() {
  const connected = useEngine((s) => s.status === "connected");
  const head = useEngine((s) => s.head);
  const changeCount = useEngine((s) => s.changes.length);
  const [entries, setEntries] = useState<Entry[]>([]);
  const [open, setOpen] = useState<Set<string>>(new Set());

  useEffect(() => {
    if (!connected) return;
    const timer = setTimeout(() => {
      engine.call<{ entries: Entry[]; head: number | null }>("getHistory").then((r) => setEntries(r.entries), () => {});
    }, 120);
    return () => clearTimeout(timer);
  }, [connected, head, changeCount]);

  const byId = useMemo(() => new Map(entries.map((e) => [e.id, e])), [entries]);

  const applied = useMemo(() => {
    const set = new Set<number>();
    for (let id = head ?? undefined; id !== undefined; id = byId.get(id)?.parent) set.add(id);
    return set;
  }, [head, byId]);

  const rows = useMemo(() => {
    const result: Row[] = [];
    for (const e of [...entries].reverse()) {
      const last = result[result.length - 1];
      if (last && e.group && last.group === e.group) last.entries.push(e);
      else result.push({ key: `${e.id}`, entries: [e], origin: e.origin, group: e.group });
    }
    return result;
  }, [entries]);

  /** Undoes back to an earlier change, or redoes forward to a later one on the branch redo follows. */
  const jumpTo = async (target: number) => {
    const back = applied.has(target);
    let current = useEngine.getState().head;
    for (let guard = 0; guard < 1000 && current !== target; guard++) {
      const r = await call<{ changed: boolean; head: number | null }>(back ? "undo" : "redo", { wholeGroup: false });
      if (!r.changed) break;
      current = r.head;
    }
  };

  if (entries.length === 0) return <Empty>No changes yet.</Empty>;

  let lastDay = "";

  return (
    <div className="h-full overflow-y-auto py-1">
      {rows.map((row) => {
        const newest = row.entries[0];
        const day = new Date(newest.time).toDateString();
        const showDay = day !== lastDay;
        lastDay = day;
        const isApplied = applied.has(newest.id);
        const isHead = row.entries.some((e) => e.id === head);
        const expanded = open.has(row.key);
        const agent = row.origin !== "ui";

        return (
          <div key={row.key}>
            {showDay && <div className="px-3 pt-2 pb-1 text-[9px] uppercase tracking-wide text-faint">{new Date(newest.time).toLocaleDateString()}</div>}
            <div
              className={cn("group flex h-[26px] cursor-pointer items-center gap-1.5 px-2 text-xs hover:bg-raised", !isApplied && "opacity-40", isHead && "bg-control")}
              onClick={() => void jumpTo(newest.id)}
              title={isApplied ? "Undo back to here" : "Redo up to here"}
            >
              {row.entries.length > 1 ? (
                <button type="button" className="text-faint hover:text-fg" onClick={(e) => {
                  e.stopPropagation();
                  setOpen((s) => {
                    const n = new Set(s);
                    if (n.has(row.key)) n.delete(row.key); else n.add(row.key);
                    return n;
                  });
                }}><ChevronRight size={11} className={cn("transition-transform", expanded && "rotate-90")} /></button>
              ) : <span className="w-[11px]" />}
              {agent ? <Bot size={11} className="shrink-0 text-agent" /> : <Monitor size={11} className="shrink-0 text-faint" />}
              <span className="min-w-0 flex-1 truncate text-fg">
                {row.entries.length > 1 ? `${row.entries.length} changes · ${row.entries[row.entries.length - 1].label}` : newest.label}
              </span>
              <span className="shrink-0 text-2xs text-faint tabular">{timeOf(newest.time)}</span>
            </div>
            {expanded && row.entries.map((e) => (
              <div key={e.id} onClick={() => void jumpTo(e.id)}
                className={cn("flex h-[22px] cursor-pointer items-center gap-1.5 pr-2 pl-9 text-2xs text-dim hover:bg-raised", !applied.has(e.id) && "opacity-40", e.id === head && "text-fg")}>
                <span className="min-w-0 flex-1 truncate">{e.label}</span>
              </div>
            ))}
          </div>
        );
      })}
      <div className="p-2">
        <Button size="sm" onClick={() => void call("undo")}><Undo2 size={12} /> Undo last</Button>
      </div>
    </div>
  );
}
