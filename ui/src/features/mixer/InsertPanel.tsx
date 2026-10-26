import type { Effect, Insert, Project } from "@groovekit/schema/project";
import { GripVertical, Plus, Power, X } from "lucide-react";
import { useState } from "react";
import { IconButton, Knob } from "@/components/controls";
import { ContextMenu, ContextMenuContent, ContextMenuItem, ContextMenuSeparator, ContextMenuTrigger, Dropdown, DropdownContent, DropdownItem, DropdownLabel, DropdownSeparator, DropdownSub, DropdownTrigger } from "@/components/menu";
import { createAutomation } from "@/features/playlist/automation";
import { PluginList } from "@/features/rack/PluginPicker";
import { effectNames, getCatalogue } from "@/lib/catalogue";
import { cn } from "@/lib/cn";
import { useLiveControl } from "@/lib/liveControl";
import { colorFor, formatDb, insertName, uniqueId } from "@/lib/model";
import { call, execute } from "@/state/engine";
import { openWindow, useWindows, windowKey } from "@/state/windows";
import { effectName } from "./Strip";

const EFFECT_MIME = "application/x-groovekit-effect";

/** The selected insert's effect chain, each slot with its on switch and mix knob, and its sends. */
export function InsertPanel({ doc, insert, index }: { doc: Project; insert: Insert; index: number }) {
  const master = insert.id === "master";
  const color = master ? "#d6d6db" : colorFor(insert.id, insert.color);
  const effects = insert.effects ?? [];
  const [dropAt, setDropAt] = useState<number | null>(null);

  const move = (id: string, to: number) => {
    const from = effects.findIndex((e) => e.id === id);
    if (from < 0) return;
    const target = to > from ? to - 1 : to;
    if (target !== from) void execute({ type: "moveEffect", insert: insert.id, effect: id, index: target }, { label: "Move effect" });
  };

  return (
    <div className="flex h-full flex-col">
      <div className="flex h-9 shrink-0 items-center gap-2 border-b border-line px-3" style={{ boxShadow: `inset 0 2px 0 ${color}` }}>
        <div className="min-w-0 flex-1 truncate text-sm font-medium">{insertName(insert, index)}</div>
        <div className="text-2xs text-faint">{master ? "Output" : `Insert ${index}`}</div>
      </div>
      <div className="min-h-0 flex-1 overflow-y-auto p-2">
        <div className="mb-1.5 flex items-center px-1 text-2xs uppercase tracking-wide text-faint">
          <span className="flex-1">Effects</span><span>Mix</span>
        </div>
        <div className="flex flex-col"
          onDragOver={(e) => {
            if (!e.dataTransfer.types.includes(EFFECT_MIME)) return;
            e.preventDefault();
            const rows = [...e.currentTarget.querySelectorAll<HTMLElement>("[data-slot]")];
            const at = rows.findIndex((r) => e.clientY < r.getBoundingClientRect().top + r.offsetHeight / 2);
            setDropAt(at < 0 ? rows.length : at);
          }}
          onDragLeave={(e) => !e.currentTarget.contains(e.relatedTarget as Node) && setDropAt(null)}
          onDrop={(e) => {
            const id = e.dataTransfer.getData(EFFECT_MIME);
            if (id && dropAt !== null) move(id, dropAt);
            setDropAt(null);
          }}>
          {effects.map((effect, i) => (
            <div key={effect.id} data-slot>
              {dropAt === i && <div className="h-0.5 rounded bg-accent" />}
              <EffectSlot doc={doc} insert={insert} effect={effect} index={i} count={effects.length} />
            </div>
          ))}
          {dropAt === effects.length && <div className="h-0.5 rounded bg-accent" />}
        </div>
        <AddEffect insert={insert} />

        <Sends doc={doc} insert={insert} />
      </div>
    </div>
  );
}

function EffectSlot({ doc, insert, effect, index, count }: { doc: Project; insert: Insert; effect: Effect; index: number; count: number }) {
  const name = effectName(effect);
  const open = useWindows((s) => s.windows.some((w) => w.key === windowKey({ kind: "effect", insert: insert.id, effect: effect.id })));
  const mix = useLiveControl((v) => ({ type: "updateEffect", insert: insert.id, effect: effect.id, changes: { mix: Math.round(v * 100) / 100 } }), `${name} mix`);
  const update = (changes: Record<string, unknown>, label: string) => void execute({ type: "updateEffect", insert: insert.id, effect: effect.id, changes }, { label });
  const show = () => openEffect(insert.id, effect);

  return (
    <ContextMenu>
      <ContextMenuTrigger asChild>
        <div
          draggable
          onDragStart={(e) => {
            e.dataTransfer.setData(EFFECT_MIME, effect.id);
            e.dataTransfer.effectAllowed = "move";
          }}
          className={cn("group flex h-8 items-center gap-1.5 rounded-[4px] px-1 hover:bg-raised", open && "bg-raised")}
        >
          <GripVertical size={11} className="shrink-0 cursor-grab text-faint opacity-0 group-hover:opacity-100" />
          <button type="button" title={effect.bypass ? "Turn on" : "Turn off"}
            className={cn("grid h-5 w-5 shrink-0 place-items-center rounded-full", effect.bypass ? "text-faint" : "text-accent")}
            onClick={() => update({ bypass: !effect.bypass }, effect.bypass ? `Turn on ${name}` : `Turn off ${name}`)}>
            <Power size={11} />
          </button>
          <button type="button" onClick={show} title={effect.type === "plugin" ? "Open the plugin's window" : "Open"}
            className={cn("min-w-0 flex-1 truncate text-left text-xs", effect.bypass ? "text-faint line-through" : "text-fg")}>
            {name}
          </button>
          <span className="w-8 text-right text-2xs text-faint tabular">{Math.round((effect.mix ?? 1) * 100)}%</span>
          <Knob size={22} value={effect.mix ?? 1} min={0} max={1} defaultValue={1} title="Mix" format={(v) => `${Math.round(v * 100)}%`}
            onChange={mix.onChange} onCommit={mix.onCommit} />
        </div>
      </ContextMenuTrigger>
      <ContextMenuContent>
        <ContextMenuItem onSelect={show}>Open</ContextMenuItem>
        <ContextMenuItem disabled={index === 0} onSelect={() => void execute({ type: "moveEffect", insert: insert.id, effect: effect.id, index: index - 1 }, { label: "Move effect" })}>Move up</ContextMenuItem>
        <ContextMenuItem disabled={index === count - 1} onSelect={() => void execute({ type: "moveEffect", insert: insert.id, effect: effect.id, index: index + 1 }, { label: "Move effect" })}>Move down</ContextMenuItem>
        <ContextMenuItem onSelect={() => void createAutomation(doc, { scope: "effect", insert: insert.id, effect: effect.id, param: "mix" }, effect.mix ?? 1)}>Automate the mix</ContextMenuItem>
        <ContextMenuSeparator />
        <ContextMenuItem danger onSelect={() => void execute({ type: "removeEffect", insert: insert.id, effect: effect.id }, { label: `Remove ${name}` })}>Remove</ContextMenuItem>
      </ContextMenuContent>
    </ContextMenu>
  );
}

/** Opens an effect: ours in a window of its own, a plugin in the plugin's window. */
export function openEffect(insert: string, effect: Effect) {
  if (effect.type === "plugin") void call("plugins.openEditor", { insert, effect: effect.id });
  else openWindow({ kind: "effect", insert, effect: effect.id });
}

function AddEffect({ insert }: { insert: Insert }) {
  const add = (effect: Effect, name: string) =>
    void execute({ type: "addEffect", insert: insert.id, effect }, { label: `Add ${name}` }).then(() => openEffect(insert.id, effect));
  const id = (base: string) => uniqueId(base, insert.effects ?? []);

  return (
    <Dropdown>
      <DropdownTrigger asChild>
        <button type="button" className="mt-1 flex h-8 w-full items-center gap-1.5 rounded-[4px] border border-dashed border-line px-2 text-xs text-faint hover:border-faint hover:text-dim">
          <Plus size={12} /> Add effect
        </button>
      </DropdownTrigger>
      <DropdownContent>
        <DropdownLabel>Built in</DropdownLabel>
        {Object.entries(effectNames).map(([kind, name]) => {
          const mix = getCatalogue()?.[kind]?.defaultMix;
          return <DropdownItem key={kind} onSelect={() => add({ type: "builtin", id: id(kind), kind: kind as never, ...(mix !== undefined && mix < 1 ? { mix } : {}) }, name)}>{name}</DropdownItem>;
        })}
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
  const sends = insert.sends ?? [];
  if (sends.length === 0 && targets.length === 0) return null;

  return (
    <div className="mt-4">
      <div className="mb-1.5 px-1 text-2xs uppercase tracking-wide text-faint">Sends</div>
      {sends.map((send) => {
        const index = doc.mixer.inserts.findIndex((i) => i.id === send.target);
        return (
          <ContextMenu key={send.id}>
            <ContextMenuTrigger asChild>
              <div className="flex h-8 items-center gap-1.5 rounded-[4px] px-1 hover:bg-raised">
                <span className="min-w-0 flex-1 truncate text-xs text-dim">→ {insertName(doc.mixer.inserts[index], index)}</span>
                <span className="text-2xs text-faint">{send.preFader ? "pre" : "post"}</span>
                <span className="w-12 text-right text-2xs text-faint tabular">{formatDb(send.levelDb ?? 0)} dB</span>
                <SendKnob insertId={insert.id} sendId={send.id} levelDb={send.levelDb ?? 0} />
                <IconButton size="sm" className="h-5 w-5" title="Remove send"
                  onClick={() => void execute({ type: "removeSend", insert: insert.id, send: send.id }, { label: "Remove send" })}><X size={10} /></IconButton>
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
            <button type="button" className="flex h-7 w-full items-center gap-1.5 rounded-[4px] px-2 text-xs text-faint hover:bg-raised hover:text-dim"><Plus size={12} /> Add send</button>
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

function SendKnob({ insertId, sendId, levelDb }: { insertId: string; sendId: string; levelDb: number }) {
  const level = useLiveControl((v) => ({ type: "updateSend", insert: insertId, send: sendId, changes: { levelDb: Math.round(v * 10) / 10 } }), "Send level");
  return (
    <Knob size={22} value={levelDb} min={-60} max={12} defaultValue={0} format={(v) => `${formatDb(v)} dB`} title="Send level"
      color="#58b7d6" onChange={level.onChange} onCommit={level.onCommit} />
  );
}
