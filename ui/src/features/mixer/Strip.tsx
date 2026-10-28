import type { Effect, Insert, Project } from "@groovekit/schema/project";
import { ChevronDown } from "lucide-react";
import { useState } from "react";
import { Knob, ToggleChip } from "@/components/controls";
import { Fader, Meter } from "@/components/fader";
import { ContextMenu, ContextMenuContent, ContextMenuItem, ContextMenuSeparator, ContextMenuSub, ContextMenuTrigger, Dropdown, DropdownContent, DropdownItem, DropdownLabel, DropdownTrigger } from "@/components/menu";
import { createAutomation } from "@/features/playlist/automation";
import { effectNames } from "@/lib/catalogue";
import { cn } from "@/lib/cn";
import { ColorSwatch } from "@/components/color";
import { colorFor, formatDb, formatPan, insertName, palette } from "@/lib/model";
import { useLiveControl } from "@/lib/liveControl";
import { useHeight } from "@/lib/useHeight";
import { execute, useEngine } from "@/state/engine";

export function effectName(effect: Effect) {
  return effect.type === "builtin" ? effectNames[effect.kind] ?? effect.kind : effect.plugin.name;
}

export function Strip({ doc, insert, index, selected, onSelect }: {
  doc: Project; insert: Insert; index: number; selected: boolean; onSelect: () => void;
}) {
  const levels = useEngine((s) => s.levels.inserts[insert.id]) ?? [0, 0];
  const touched = useEngine((s) => s.touchedAt[insert.id]);
  const [liveDb, setLiveDb] = useState<number | null>(null);
  const master = insert.id === "master";
  const color = master ? "#d6d6db" : colorFor(insert.id, insert.color);
  const update = (changes: Record<string, unknown>, label: string) => void execute({ type: "updateInsert", insert: insert.id, changes }, { label });
  const db = liveDb ?? insert.volumeDb ?? 0;
  const feeders = doc.channels.filter((c) => (c.insert ?? "master") === insert.id).length;
  const [faderArea, faderAreaHeight] = useHeight<HTMLDivElement>();
  const volume = useLiveControl((v) => ({ type: "updateInsert", insert: insert.id, changes: { volumeDb: v } }), `${insertName(insert, index)} volume`);
  const pan = useLiveControl((v) => ({ type: "updateInsert", insert: insert.id, changes: { pan: Math.round(v * 100) / 100 } }), "Pan insert");
  const faderH = Math.max(20, faderAreaHeight - 4);

  return (
    <div className={cn("relative flex h-full w-[92px] shrink-0 flex-col bg-panel", selected && "bg-raised", insert.mute && "opacity-70")}
      onPointerDownCapture={onSelect}>
      {selected && <span className="pointer-events-none absolute inset-0 z-10 ring-1 ring-accent/70 ring-inset" />}
      <StripName insert={insert} index={index} color={color} />

      {/* What's in the insert, at a glance; the chain is edited in the panel beside the strips. */}
      <div className="flex h-[96px] shrink-0 flex-col gap-px overflow-hidden px-1.5 pt-1.5 pb-1">
        {(insert.effects ?? []).slice(0, 4).map((effect) => (
          <div key={effect.id} className="flex h-[15px] items-center gap-1 text-[9px] leading-none">
            <span className={cn("h-1.5 w-1.5 shrink-0 rounded-full", effect.bypass ? "bg-faint/50" : "bg-accent")} />
            <span className={cn("truncate", effect.bypass ? "text-faint line-through" : "text-dim")}>{effectName(effect)}</span>
          </div>
        ))}
        {(insert.effects?.length ?? 0) > 4 && <div className="text-[9px] text-faint">+{insert.effects!.length - 4} more</div>}
        {(insert.sends?.length ?? 0) > 0 && (
          <div className="truncate text-[9px] text-[#58b7d6]">→ {insert.sends!.length} send{insert.sends!.length === 1 ? "" : "s"}</div>
        )}
      </div>
      {master ? <div className="h-[20px] shrink-0" /> : <OutputPicker doc={doc} insert={insert} />}

      <div className="flex shrink-0 justify-center py-1.5">
        <ContextMenu>
          <ContextMenuTrigger asChild>
            <div>
              <Knob size={26} value={insert.pan ?? 0} min={-1} max={1} defaultValue={0} bipolar title="Pan" format={formatPan}
                onChange={pan.onChange} onCommit={pan.onCommit} />
            </div>
          </ContextMenuTrigger>
          <ContextMenuContent>
            <ContextMenuItem onSelect={() => void createAutomation(doc, { scope: "insert", insert: insert.id, param: "pan" }, insert.pan ?? 0)}>Create automation clip</ContextMenuItem>
          </ContextMenuContent>
        </ContextMenu>
      </div>

      <ContextMenu>
        <ContextMenuTrigger asChild>
          <div ref={faderArea} className="flex min-h-0 flex-1 items-end justify-center gap-1.5 px-2">
            <Fader db={insert.volumeDb ?? 0} height={faderH}
              onChange={(v) => {
                setLiveDb(v);
                volume.onChange(v);
              }}
              onCommit={(v) => {
                setLiveDb(null);
                volume.onCommit(v);
              }} />
            <Meter left={levels[0]} right={levels[1]} height={faderH - 6} />
          </div>
        </ContextMenuTrigger>
        <ContextMenuContent>
          <ContextMenuItem onSelect={() => update({ volumeDb: 0 }, "Reset volume")}>Reset to 0 dB</ContextMenuItem>
          <ContextMenuItem onSelect={() => void createAutomation(doc, { scope: "insert", insert: insert.id, param: "volume" }, insert.volumeDb ?? 0)}>Create automation clip</ContextMenuItem>
        </ContextMenuContent>
      </ContextMenu>

      <div className="flex shrink-0 items-center justify-between px-2 py-1.5">
        <span className="font-mono text-2xs text-dim tabular">{formatDb(db)}</span>
        <div className="flex gap-0.5">
          {!master && (
            <ToggleChip on={!!insert.solo} label="S" title={insert.solo ? "Unsolo" : "Solo: hear only this insert, with what feeds it and what it feeds"}
              onClick={() => update({ solo: !insert.solo }, insert.solo ? "Unsolo insert" : "Solo insert")} />
          )}
          <ToggleChip on={!insert.mute} label={master ? "M" : String(index)} title={insert.mute ? "Unmute" : "Mute"}
            onClick={() => update({ mute: !insert.mute }, insert.mute ? "Unmute insert" : "Mute insert")} />
        </div>
      </div>
      <div className="h-4 shrink-0 px-2 text-[9px] text-faint">{feeders > 0 && `${feeders} channel${feeders === 1 ? "" : "s"}`}</div>

      {touched !== undefined && Date.now() - touched < 2000 && (
        <span key={touched} className="pointer-events-none absolute inset-0 animate-agent-flash bg-agent/20 ring-1 ring-agent ring-inset" />
      )}
    </div>
  );
}

function StripName({ insert, index, color }: { insert: Insert; index: number; color: string }) {
  const [editing, setEditing] = useState(false);
  const master = insert.id === "master";

  if (editing) {
    return (
      <input
        autoFocus defaultValue={insert.name ?? ""}
        className="m-1 h-6 rounded-[3px] bg-bg px-1 text-xs outline-none ring-1 ring-accent"
        onBlur={(e) => {
          setEditing(false);
          const v = e.target.value.trim();
          if (v && v !== insert.name) void execute({ type: "updateInsert", insert: insert.id, changes: { name: v } }, { label: "Rename insert" });
        }}
        onKeyDown={(e) => {
          if (e.key === "Enter") (e.target as HTMLInputElement).blur();
          if (e.key === "Escape") setEditing(false);
          e.stopPropagation();
        }}
      />
    );
  }

  return (
    <ContextMenu>
      <ContextMenuTrigger asChild>
        <div className="flex h-12 shrink-0 flex-col justify-center gap-0.5 border-b border-line px-2 pt-1.5 pb-1" style={{ boxShadow: `inset 0 4px 0 ${color}` }}
          onDoubleClick={() => !master && setEditing(true)}>
          <div className="truncate text-xs font-medium text-fg">{insertName(insert, index)}</div>
          <div className="flex items-center gap-1">
            <span className="flex-1 text-[9px] text-faint">{master ? "Output" : `Insert ${index}`}</span>
            {!master && (
              <ColorSwatch color={color} chosen={!!insert.color} size={10} title="Colour"
                onChange={(c) => void execute({ type: "updateInsert", insert: insert.id, changes: { color: c } }, { label: c ? "Colour insert" : "Reset insert colour" })} />
            )}
          </div>
        </div>
      </ContextMenuTrigger>
      <ContextMenuContent>
        {!master && <ContextMenuItem onSelect={() => setEditing(true)}>Rename</ContextMenuItem>}
        {!master && (
          <ContextMenuSub label="Colour">
            <div className="grid grid-cols-6 gap-1 p-1">
              {palette.map((c) => (
                <button key={c} type="button" className="h-5 w-5 rounded-[3px]" style={{ background: c }}
                  onClick={() => void execute({ type: "updateInsert", insert: insert.id, changes: { color: c } }, { label: "Colour insert" })} />
              ))}
            </div>
          </ContextMenuSub>
        )}
        {!master && <ContextMenuSeparator />}
        <ContextMenuItem disabled={master} danger onSelect={() => void execute({ type: "removeInsert", insert: insert.id }, { label: `Remove ${insertName(insert, index)}` })}>
          Remove insert
        </ContextMenuItem>
      </ContextMenuContent>
    </ContextMenu>
  );
}

function OutputPicker({ doc, insert }: { doc: Project; insert: Insert }) {
  const output = insert.output ?? "master";
  const index = doc.mixer.inserts.findIndex((i) => i.id === output);
  return (
    <Dropdown>
      <DropdownTrigger asChild>
        <button type="button" className="mx-1.5 flex h-[20px] shrink-0 items-center gap-1 rounded-[3px] bg-control px-1 text-2xs text-dim hover:text-fg" title="Output">
          <span className="flex-1 truncate text-left">→ {insertName(doc.mixer.inserts[index], index)}</span><ChevronDown size={9} />
        </button>
      </DropdownTrigger>
      <DropdownContent>
        <DropdownLabel>Output to</DropdownLabel>
        {doc.mixer.inserts.filter((i) => i.id !== insert.id).map((t) => {
          const i = doc.mixer.inserts.indexOf(t);
          return (
            <DropdownItem key={t.id} checked={t.id === output}
              onSelect={() => void execute({ type: "updateInsert", insert: insert.id, changes: { output: t.id === "master" ? null : t.id } }, { label: `Route to ${insertName(t, i)}` })}>
              {insertName(t, i)}
            </DropdownItem>
          );
        })}
      </DropdownContent>
    </Dropdown>
  );
}
