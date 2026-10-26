import { Bot, Undo2 } from "lucide-react";
import { useEffect, useState } from "react";
import { Button, Empty } from "@/components/controls";
import { cn } from "@/lib/cn";
import { call, useEngine, type ChangeEvent } from "@/state/engine";
import { setUi } from "@/state/ui";

function ago(t: number, now: number) {
  const s = Math.round((now - t) / 1000);
  if (s < 5) return "now";
  if (s < 60) return `${s}s`;
  if (s < 3600) return `${Math.round(s / 60)}m`;
  return new Date(t).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" });
}

/** What the agent (or any client other than this UI) changed, live. Bursts within 20 s form a request. */
export function Activity() {
  const changes = useEngine((s) => s.changes);
  const doc = useEngine((s) => s.doc);
  const [now, setNow] = useState(Date.now());

  useEffect(() => {
    const timer = setInterval(() => setNow(Date.now()), 5000);
    return () => clearInterval(timer);
  }, []);

  const foreign = changes.filter((c) => c.origin !== "ui");
  if (foreign.length === 0) {
    return <Empty>Changes the agent makes show up here as they happen, and flash in the playlist, channel rack and mixer.</Empty>;
  }

  const bursts: ChangeEvent[][] = [];
  for (const c of foreign) {
    const last = bursts[bursts.length - 1];
    if (last && last[last.length - 1].time - c.time < 20000) last.push(c);
    else bursts.push([c]);
  }

  const reveal = (c: ChangeEvent) => {
    if (!doc) return;
    const clips = c.touched.filter((id) => doc.playlist.clips.some((x) => x.id === id));
    const channel = c.touched.find((id) => doc.channels.some((x) => x.id === id));
    const pattern = c.touched.find((id) => doc.patterns.some((x) => x.id === id));
    const insert = c.touched.find((id) => doc.mixer.inserts.some((x) => x.id === id));
    if (clips.length) setUi({ selectedClips: clips });
    if (pattern) setUi({ selectedPattern: pattern, bottomOpen: true, bottomPanel: "rack" });
    if (channel) setUi({ selectedChannel: channel, bottomOpen: true, bottomPanel: "rack" });
    if (insert && !channel) setUi({ bottomOpen: true, bottomPanel: "mixer" });
  };

  return (
    <div className="flex h-full flex-col">
      <div className="min-h-0 flex-1 overflow-y-auto py-1">
        {bursts.map((burst, i) => (
          <div key={burst[0].entry + "-" + i} className="mb-1">
            <div className="flex items-center gap-1.5 px-3 pt-2 pb-1 text-[9px] uppercase tracking-wide text-faint">
              <Bot size={10} className="text-agent" /> {burst[0].origin} · {burst.length} change{burst.length === 1 ? "" : "s"} · {ago(burst[0].time, now)}
            </div>
            {burst.map((c, j) => (
              <div key={j} onClick={() => reveal(c)}
                className={cn("flex h-[24px] cursor-pointer items-center gap-2 px-3 text-xs hover:bg-raised", now - c.time < 2500 && "text-agent")}>
                <span className={cn("h-1.5 w-1.5 shrink-0 rounded-full", c.kind === "execute" ? "bg-agent" : "bg-faint")} />
                <span className="min-w-0 flex-1 truncate">{c.kind !== "execute" ? `${c.kind === "undo" ? "Undo" : "Redo"}: ` : ""}{c.label}</span>
              </div>
            ))}
          </div>
        ))}
      </div>
      <div className="border-t border-line p-2">
        <Button size="sm" onClick={() => void call("undo", { wholeGroup: true })} title="Undoes the last change together with the rest of its request">
          <Undo2 size={12} /> Undo the last request
        </Button>
      </div>
    </div>
  );
}
