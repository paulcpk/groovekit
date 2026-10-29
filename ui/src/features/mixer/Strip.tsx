import type { Effect, Insert } from "@groovekit/schema/project";
import { ChevronDown } from "lucide-react";
import { memo, useEffect, useState } from "react";
import { Knob, ToggleChip } from "@/components/controls";
import { Fader, Meter } from "@/components/fader";
import { Dropdown, DropdownContent, DropdownItem, DropdownLabel, DropdownTrigger } from "@/components/menu";
import { effectNames } from "@/lib/catalogue";
import { cn } from "@/lib/cn";
import { ColorSwatch } from "@/components/color";
import { colorFor, formatDb, formatPan, insertName } from "@/lib/model";
import { useAutomated } from "@/lib/automation";
import { useLiveControl } from "@/lib/liveControl";
import { useHeight } from "@/lib/useHeight";
import { execute, useEngine } from "@/state/engine";

export function effectName(effect: Effect) {
  // The EQ that keeps a channel's part to its frequency range.
  if (effect.type === "builtin" && effect.kind === "eq" && effect.id === "slot") return "Slot EQ";
  return effect.type === "builtin" ? effectNames[effect.kind] ?? effect.kind : effect.plugin.name;
}

/** A strip is drawn again only when its insert, place or selection changes; its meter, pan and
    fader follow the levels and automation on their own. */
export const Strip = memo(function Strip({ insert, index, selected, onSelect, renaming, onRenamed }: {
  insert: Insert; index: number; selected: boolean; onSelect: (insert: string) => void;
  /** The mixer's menu asked for a new name: show the field. */
  renaming: boolean; onRenamed: () => void;
}) {
  const touched = useEngine((s) => s.touchedAt[insert.id]);
  const master = insert.id === "master";
  const color = master ? "#d6d6db" : colorFor(insert.id, insert.color);
  const update = (changes: Record<string, unknown>, label: string) => void execute({ type: "updateInsert", insert: insert.id, changes }, { label });
  const feeders = useEngine((s) => s.doc?.channels.filter((c) => (c.insert ?? "master") === insert.id).length ?? 0);
  const [faderArea, faderAreaHeight] = useHeight<HTMLDivElement>();
  const faderH = Math.max(20, faderAreaHeight - 4);
  // The fader's value while it's dragged, for the reading below.
  const [liveDb, setLiveDb] = useState<number | null>(null);

  return (
    <div className={cn("relative flex h-full w-[92px] shrink-0 flex-col bg-panel", selected && "bg-raised", insert.mute && "opacity-70")}
      data-insert={insert.id} onPointerDownCapture={() => onSelect(insert.id)}>
      {selected && <span className="pointer-events-none absolute inset-0 z-10 ring-1 ring-accent/70 ring-inset" />}
      <StripName insert={insert} index={index} color={color} renaming={renaming} onRenamed={onRenamed} />

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
      {master ? <div className="h-[20px] shrink-0" /> : <OutputPicker insert={insert} />}

      <StripPan insert={insert} />

      <div ref={faderArea} className="flex min-h-0 flex-1">
        <StripFader insert={insert} index={index} height={faderH} onLive={setLiveDb} />
      </div>

      <div className="flex shrink-0 items-center justify-between px-2 py-1.5">
        <StripReading insert={insert} liveDb={liveDb} />
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
});

/** The insert's meter, on its own so the levels (thirty times a second) redraw only it. */
function StripMeter({ insert, height }: { insert: string; height: number }) {
  const levels = useEngine((s) => s.levels.inserts[insert]) ?? [0, 0];
  return <Meter left={levels[0]} right={levels[1]} height={height} />;
}

/** The pan knob, turning with its automation as the song plays. */
function StripPan({ insert }: { insert: Insert }) {
  const automatedPan = useAutomated({ scope: "insert", insert: insert.id, param: "pan" }, insert.pan ?? 0);
  const pan = useLiveControl((v) => ({ type: "updateInsert", insert: insert.id, changes: { pan: Math.round(v * 100) / 100 } }), "Pan insert");

  return (
    <div className="flex shrink-0 justify-center py-1.5">
      <Knob size={26} value={automatedPan} min={-1} max={1} defaultValue={0} bipolar title="Pan" format={formatPan}
        automation={{ scope: "insert", insert: insert.id, param: "pan" }} automationValue={insert.pan ?? 0}
        onChange={pan.onChange} onCommit={pan.onCommit} />
    </div>
  );
}

/** The fader with its meter, moving with its automation as the song plays; onLive gets the
    value while it's dragged, null on release. */
function StripFader({ insert, index, height, onLive }: { insert: Insert; index: number; height: number; onLive: (db: number | null) => void }) {
  const automatedDb = useAutomated({ scope: "insert", insert: insert.id, param: "volume" }, insert.volumeDb ?? 0);
  const volume = useLiveControl((v) => ({ type: "updateInsert", insert: insert.id, changes: { volumeDb: v } }), `${insertName(insert, index)} volume`);

  return (
    <div className="flex min-h-0 flex-1 items-end justify-center gap-1.5 px-2">
      <Fader db={automatedDb} height={height} automation={{ scope: "insert", insert: insert.id, param: "volume" }}
        onChange={(v) => {
          onLive(v);
          volume.onChange(v);
        }}
        onCommit={(v) => {
          onLive(null);
          volume.onCommit(v);
        }} />
      <StripMeter insert={insert.id} height={height - 6} />
    </div>
  );
}

/** The fader's reading: the value being dragged, else the automated or set one. */
function StripReading({ insert, liveDb }: { insert: Insert; liveDb: number | null }) {
  const automatedDb = useAutomated({ scope: "insert", insert: insert.id, param: "volume" }, insert.volumeDb ?? 0);
  return <span className="font-mono text-2xs text-dim tabular">{formatDb(liveDb ?? automatedDb)}</span>;
}

/** The insert's name and colour; double-click (or the mixer's menu) to rename. The menu is the
    mixer's, shared by all the strips. */
function StripName({ insert, index, color, renaming, onRenamed }: { insert: Insert; index: number; color: string; renaming: boolean; onRenamed: () => void }) {
  const [editing, setEditing] = useState(false);
  const master = insert.id === "master";

  useEffect(() => {
    if (renaming && !master) setEditing(true);
  }, [renaming, master]);

  const done = () => {
    setEditing(false);
    onRenamed();
  };

  if (editing) {
    return (
      <input
        autoFocus defaultValue={insert.name ?? ""}
        className="m-1 h-6 rounded-[3px] bg-bg px-1 text-xs outline-none ring-1 ring-accent"
        onBlur={(e) => {
          done();
          const v = e.target.value.trim();
          if (v && v !== insert.name) void execute({ type: "updateInsert", insert: insert.id, changes: { name: v } }, { label: "Rename insert" });
        }}
        onKeyDown={(e) => {
          if (e.key === "Enter") (e.target as HTMLInputElement).blur();
          if (e.key === "Escape") done();
          e.stopPropagation();
        }}
      />
    );
  }

  return (
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
  );
}

function OutputPicker({ insert }: { insert: Insert }) {
  const inserts = useEngine((s) => s.doc?.mixer.inserts ?? []);
  const output = insert.output ?? "master";
  const index = inserts.findIndex((i) => i.id === output);
  return (
    <Dropdown>
      <DropdownTrigger asChild>
        <button type="button" className="mx-1.5 flex h-[20px] shrink-0 items-center gap-1 rounded-[3px] bg-control px-1 text-2xs text-dim hover:text-fg" title="Output">
          <span className="flex-1 truncate text-left">→ {insertName(inserts[index], index)}</span><ChevronDown size={9} />
        </button>
      </DropdownTrigger>
      <DropdownContent>
        <DropdownLabel>Output to</DropdownLabel>
        {inserts.filter((i) => i.id !== insert.id).map((t) => {
          const i = inserts.indexOf(t);
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
