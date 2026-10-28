import type { PlaylistRow, Project } from "@groovekit/schema/project";
import { useState } from "react";
import { ToggleChip } from "@/components/controls";
import { ContextMenu, ContextMenuContent, ContextMenuItem, ContextMenuSeparator, ContextMenuSub, ContextMenuTrigger } from "@/components/menu";
import { cn } from "@/lib/cn";
import { palette } from "@/lib/model";
import { execute, useEngine } from "@/state/engine";
import { HEADER_W } from "./geometry";

/** An arrangement row's name, mute and solo. Rows only arrange: where clips play and how loud is
    set by their channels, so a row with nothing in it is just empty. Pressing the header selects
    the row (Shift adds the rows in between) and dragging it moves the selected rows. */
export function RowHeader({ row, index, doc, height, selected, onPress }: {
  row: PlaylistRow; index: number; doc: Project; height: number; selected: boolean;
  onPress: (e: React.PointerEvent, index: number) => void;
}) {
  const [editing, setEditing] = useState(false);
  const touched = useEngine((s) => s.touchedAt[`row:${index}`]);
  const clips = doc.playlist.clips.filter((c) => c.row === index).length;
  const name = row.name || `Row ${index + 1}`;
  const update = (changes: Partial<PlaylistRow>, label: string) => void execute({ type: "updateRow", row: index, changes }, { label });

  return (
    <ContextMenu>
      <ContextMenuTrigger asChild>
        <div
          className={cn("sticky left-0 z-10 flex shrink-0 cursor-grab items-center gap-1.5 border-r border-b border-line pr-1.5 active:cursor-grabbing",
            selected ? "bg-hover" : "bg-raised")}
          style={{ width: HEADER_W, height }}
          onPointerDown={(e) => {
            if (e.button !== 0 || editing || (e.target as HTMLElement).closest("button, input")) return;
            onPress(e, index);
          }}
        >
          <div className="h-full w-1 shrink-0" style={{ background: row.color ?? "transparent" }} />
          <div className="min-w-0 flex-1">
            {editing ? (
              <input
                autoFocus defaultValue={row.name ?? ""} placeholder={`Row ${index + 1}`}
                className="w-full rounded-[3px] bg-bg px-1 text-sm text-fg outline-none ring-1 ring-accent"
                onBlur={(e) => {
                  setEditing(false);
                  const v = e.target.value.trim();
                  if (v !== (row.name ?? "")) update({ name: v }, "Rename row");
                }}
                onKeyDown={(e) => {
                  if (e.key === "Enter") (e.target as HTMLInputElement).blur();
                  if (e.key === "Escape") setEditing(false);
                  e.stopPropagation();
                }}
              />
            ) : (
              <div className={cn("truncate text-sm", clips || row.name ? "text-fg" : "text-faint")} onDoubleClick={() => setEditing(true)}>{name}</div>
            )}
          </div>
          <ToggleChip
            on={!row.mute} tone="accent" label={String(index + 1)} title={row.mute ? "Unmute row" : "Mute row"}
            onClick={() => update({ mute: !row.mute }, row.mute ? "Unmute row" : "Mute row")}
          />
          <ToggleChip
            on={!!row.solo} label="S" title={row.solo ? "Unsolo row" : "Solo: only soloed rows play"}
            onClick={() => update({ solo: !row.solo }, row.solo ? "Unsolo row" : "Solo row")}
          />
          {touched !== undefined && Date.now() - touched < 2000 && (
            <span key={touched} className="pointer-events-none absolute inset-0 animate-agent-flash bg-agent/25" />
          )}
        </div>
      </ContextMenuTrigger>
      <ContextMenuContent>
        <ContextMenuItem onSelect={() => setEditing(true)}>Rename</ContextMenuItem>
        <ContextMenuSub label="Colour">
          <div className="grid grid-cols-6 gap-1 p-1">
            {palette.map((c) => (
              <button key={c} type="button" className="h-5 w-5 rounded-[3px]" style={{ background: c }}
                onClick={() => update({ color: c }, "Colour row")} />
            ))}
          </div>
        </ContextMenuSub>
        <ContextMenuSeparator />
        <ContextMenuItem onSelect={() => void execute({ type: "moveRows", row: index, to: index - 1 }, { label: "Move row up" })} disabled={index === 0}>
          Move up
        </ContextMenuItem>
        <ContextMenuItem onSelect={() => void execute({ type: "moveRows", row: index, to: index + 1 }, { label: "Move row down" })}>Move down</ContextMenuItem>
        <ContextMenuSeparator />
        <ContextMenuItem onSelect={() => void execute({ type: "insertRow", row: index }, { label: "Insert row" })}>Insert row above</ContextMenuItem>
        <ContextMenuItem onSelect={() => void execute({ type: "insertRow", row: index + 1 }, { label: "Insert row" })}>Insert row below</ContextMenuItem>
        <ContextMenuSeparator />
        <ContextMenuItem danger onSelect={() => void execute({ type: "removeRow", row: index }, { label: "Delete row" })}>
          Delete row{clips ? ` and ${clips} clip${clips === 1 ? "" : "s"}` : ""}
        </ContextMenuItem>
      </ContextMenuContent>
    </ContextMenu>
  );
}
