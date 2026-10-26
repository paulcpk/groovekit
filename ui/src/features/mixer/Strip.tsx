import type { Effect, Insert, Project } from "@groovekit/schema/project";
import { ChevronDown, Plus, Power, X } from "lucide-react";
import { useState } from "react";
import { IconButton, Knob, ToggleChip } from "@/components/controls";
import { Fader, Meter } from "@/components/fader";
import { ContextMenu, ContextMenuContent, ContextMenuItem, ContextMenuSeparator, ContextMenuSub, ContextMenuTrigger, Dropdown, DropdownContent, DropdownItem, DropdownLabel, DropdownSeparator, DropdownSub, DropdownTrigger } from "@/components/menu";
import { createAutomation } from "@/features/playlist/automation";
import { PluginList } from "@/features/rack/PluginPicker";
import { effectNames } from "@/lib/catalogue";
import { cn } from "@/lib/cn";
import { colorFor, formatDb, formatPan, insertName, palette, uniqueId } from "@/lib/model";
import { useHeight } from "@/lib/useHeight";
import { call, execute, useEngine } from "@/state/engine";

export function effectName(effect: Effect) {
  return effect.type === "builtin" ? effectNames[effect.kind] ?? effect.kind : effect.plugin.name;
}

export function Strip({ doc, insert, index, openEffect, onOpenEffect }: {
  doc: Project; insert: Insert; index: number; openEffect?: string; onOpenEffect: (id: string | null) => void;
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
  const faderH = Math.max(60, faderAreaHeight - 4);

  return (
    <div className={cn("relative flex h-full w-[92px] shrink-0 flex-col bg-panel", insert.mute && "opacity-70")}>
      <StripName insert={insert} index={index} color={color} />

      <div className="max-h-[45%] shrink-0 overflow-y-auto">
      <div className="flex flex-col gap-px px-1.5 pt-1.5">
        {(insert.effects ?? []).map((effect, i) => (
          <ContextMenu key={effect.id}>
            <ContextMenuTrigger asChild>
              <div
                className={cn(
                  "flex h-[20px] cursor-pointer items-center gap-1 rounded-[3px] px-1 text-2xs",
                  openEffect === effect.id ? "bg-accent/25 text-fg" : "bg-control text-dim hover:text-fg",
                )}
                onClick={() => onOpenEffect(openEffect === effect.id ? null : effect.id)}
                onDoubleClick={() => effect.type === "plugin" && void call("plugins.openEditor", { insert: insert.id, effect: effect.id })}
                title={effect.type === "plugin" ? "Double-click to open the plugin's window" : undefined}
              >
                <button
                  type="button" title={effect.bypass ? "Turn on" : "Bypass"}
                  className={effect.bypass ? "text-faint" : "text-accent"}
                  onClick={(e) => {
                    e.stopPropagation();
                    void execute({ type: "updateEffect", insert: insert.id, effect: effect.id, changes: { bypass: !effect.bypass } }, { label: effect.bypass ? "Turn effect on" : "Bypass effect" });
                  }}
                ><Power size={9} /></button>
                <span className={cn("truncate", effect.bypass && "line-through")}>{effectName(effect)}</span>
              </div>
            </ContextMenuTrigger>
            <ContextMenuContent>
              <ContextMenuItem onSelect={() => onOpenEffect(effect.id)}>Edit</ContextMenuItem>
              <ContextMenuItem disabled={i === 0} onSelect={() => void execute({ type: "moveEffect", insert: insert.id, effect: effect.id, index: i - 1 }, { label: "Move effect" })}>Move up</ContextMenuItem>
              <ContextMenuItem disabled={i === (insert.effects?.length ?? 0) - 1} onSelect={() => void execute({ type: "moveEffect", insert: insert.id, effect: effect.id, index: i + 1 }, { label: "Move effect" })}>Move down</ContextMenuItem>
              <ContextMenuSeparator />
              <ContextMenuItem danger onSelect={() => void execute({ type: "removeEffect", insert: insert.id, effect: effect.id }, { label: `Remove ${effectName(effect)}` })}>Remove</ContextMenuItem>
            </ContextMenuContent>
          </ContextMenu>
        ))}
        <AddEffect insert={insert} onAdded={onOpenEffect} />
      </div>

      <Sends doc={doc} insert={insert} />
      </div>

      {!master && <OutputPicker doc={doc} insert={insert} />}

      <div className="flex shrink-0 justify-center py-1.5">
        <ContextMenu>
          <ContextMenuTrigger asChild>
            <div>
              <Knob size={26} value={insert.pan ?? 0} min={-1} max={1} defaultValue={0} bipolar title="Pan" format={formatPan}
                onCommit={(v) => update({ pan: Math.round(v * 100) / 100 }, "Pan insert")} />
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
            <Fader db={insert.volumeDb ?? 0} height={faderH} onChange={setLiveDb}
              onCommit={(v) => {
                setLiveDb(null);
                update({ volumeDb: v }, `${insertName(insert, index)} volume`);
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
        <ToggleChip on={!insert.mute} label={master ? "M" : String(index)} title={insert.mute ? "Unmute" : "Mute"}
          onClick={() => update({ mute: !insert.mute }, insert.mute ? "Unmute insert" : "Mute insert")} />
      </div>
      {feeders > 0 && <div className="shrink-0 px-2 pb-1 text-[9px] text-faint">{feeders} channel{feeders === 1 ? "" : "s"}</div>}

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
        <div className="flex h-8 shrink-0 flex-col justify-center border-b border-line px-2" style={{ boxShadow: `inset 0 2px 0 ${color}` }}
          onDoubleClick={() => !master && setEditing(true)}>
          <div className="truncate text-xs font-medium text-fg">{insertName(insert, index)}</div>
          <div className="text-[9px] text-faint">{master ? "Output" : `Insert ${index}`}</div>
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

function AddEffect({ insert, onAdded }: { insert: Insert; onAdded: (id: string) => void }) {
  const add = (effect: Effect, name: string) =>
    void execute({ type: "addEffect", insert: insert.id, effect }, { label: `Add ${name}` }).then(() => onAdded(effect.id));
  const id = (base: string) => uniqueId(base, insert.effects ?? []);

  return (
    <Dropdown>
      <DropdownTrigger asChild>
        <button type="button" className="flex h-[20px] items-center gap-1 rounded-[3px] border border-dashed border-line px-1 text-2xs text-faint hover:border-faint hover:text-dim">
          <Plus size={9} /> Effect
        </button>
      </DropdownTrigger>
      <DropdownContent>
        <DropdownLabel>Built in</DropdownLabel>
        {Object.entries(effectNames).map(([kind, name]) => (
          <DropdownItem key={kind} onSelect={() => add({ type: "builtin", id: id(kind), kind: kind as never }, name)}>{name}</DropdownItem>
        ))}
        <DropdownSeparator />
        <DropdownSub label="Plugins">
          <PluginList instrument={false} onPick={(p) => add({ type: "plugin", id: id(p.name), plugin: { format: p.format, uid: p.uid, name: p.name, manufacturer: p.manufacturer } }, p.name)} />
        </DropdownSub>
      </DropdownContent>
    </Dropdown>
  );
}

function Sends({ doc, insert }: { doc: Project; insert: Insert }) {
  const targets = doc.mixer.inserts.filter((i) => i.id !== insert.id && !(insert.sends ?? []).some((s) => s.target === i.id));

  return (
    <div className="mt-1.5 flex flex-col gap-1 border-t border-line px-1.5 pt-1.5">
      {(insert.sends ?? []).map((send) => {
        const index = doc.mixer.inserts.findIndex((i) => i.id === send.target);
        return (
          <ContextMenu key={send.id}>
            <ContextMenuTrigger asChild>
              <div className="flex items-center gap-1">
                <Knob size={18} value={send.levelDb ?? 0} min={-60} max={12} defaultValue={0} format={(v) => `${formatDb(v)} dB`} title={`Send to ${insertName(doc.mixer.inserts[index], index)}`}
                  color="#58b7d6"
                  onCommit={(v) => void execute({ type: "updateSend", insert: insert.id, send: send.id, changes: { levelDb: Math.round(v * 10) / 10 } }, { label: "Send level" })} />
                <span className="flex-1 truncate text-2xs text-dim">→ {insertName(doc.mixer.inserts[index], index)}</span>
                <IconButton size="sm" className="h-4 w-4" title="Remove send"
                  onClick={() => void execute({ type: "removeSend", insert: insert.id, send: send.id }, { label: "Remove send" })}><X size={9} /></IconButton>
              </div>
            </ContextMenuTrigger>
            <ContextMenuContent>
              <ContextMenuItem onSelect={() => void execute({ type: "updateSend", insert: insert.id, send: send.id, changes: { preFader: !send.preFader } }, { label: "Send position" })}>
                {send.preFader ? "Post-fader" : "Pre-fader"}
              </ContextMenuItem>
              <ContextMenuItem onSelect={() => void createAutomation(doc, { scope: "send", insert: insert.id, send: send.id, param: "level" }, send.levelDb ?? 0)}>Create automation clip</ContextMenuItem>
            </ContextMenuContent>
          </ContextMenu>
        );
      })}
      {targets.length > 0 && (
        <Dropdown>
          <DropdownTrigger asChild>
            <button type="button" className="flex h-[18px] items-center gap-1 text-2xs text-faint hover:text-dim"><Plus size={9} /> Send</button>
          </DropdownTrigger>
          <DropdownContent>
            {targets.map((t) => {
              const i = doc.mixer.inserts.indexOf(t);
              return (
                <DropdownItem key={t.id} onSelect={() => void execute({ type: "addSend", insert: insert.id, send: { id: uniqueId(`to-${t.id}`, insert.sends ?? []), target: t.id, levelDb: -12 } }, { label: `Send to ${insertName(t, i)}` })}>
                  {insertName(t, i)}
                </DropdownItem>
              );
            })}
          </DropdownContent>
        </Dropdown>
      )}
    </div>
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
