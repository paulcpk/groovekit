import type { Project } from "@groovekit/schema/project";
import { ChevronDown, Minus, Plus, ScrollText } from "lucide-react";
import { useShallow } from "zustand/react/shallow";
import { Button, IconButton } from "@/components/controls";
import { Dropdown, DropdownContent, DropdownItem, DropdownSeparator, DropdownTrigger } from "@/components/menu";
import { colorFor } from "@/lib/model";
import { setUi, useUi } from "@/state/ui";
import { newPattern } from "./edits";

/** The pattern a click places, following the playhead, the row height and the zoom. */
export function PlaylistToolbar({ doc }: { doc: Project }) {
  const { selectedPattern, playlistZoom, rowHeight, followPlayhead } = useUi(useShallow((s) =>
    ({ selectedPattern: s.selectedPattern, playlistZoom: s.playlistZoom, rowHeight: s.rowHeight, followPlayhead: s.followPlayhead })));
  const pattern = doc.patterns.find((p) => p.id === selectedPattern);

  return (
    <div className="flex h-8 shrink-0 items-center gap-2 border-b border-line bg-panel px-2">
      <Dropdown>
        <DropdownTrigger asChild>
          <Button size="sm" title="The pattern a click in the playlist places">
            <span className="h-2.5 w-2.5 rounded-[2px]" style={{ background: pattern ? colorFor(pattern.id, pattern.color) : "#444" }} />
            <span className="max-w-[160px] truncate text-fg">{pattern ? pattern.name ?? pattern.id : "No pattern"}</span>
            <ChevronDown size={11} />
          </Button>
        </DropdownTrigger>
        <DropdownContent>
          {doc.patterns.map((p) => (
            <DropdownItem key={p.id} checked={p.id === selectedPattern} onSelect={() => setUi({ selectedPattern: p.id })}>
              {p.name ?? p.id}
            </DropdownItem>
          ))}
          {doc.patterns.length > 0 && <DropdownSeparator />}
          <DropdownItem onSelect={() => void newPattern(doc)}>New pattern</DropdownItem>
        </DropdownContent>
      </Dropdown>
      <div className="text-2xs text-faint">Click a lane to place it · drag to select · Alt-drag to copy</div>
      <div className="flex-1" />
      <IconButton size="sm" title="Follow the playhead" active={followPlayhead} onClick={() => setUi({ followPlayhead: !followPlayhead })}>
        <ScrollText size={13} />
      </IconButton>
      <Button size="sm" title="Row height" onClick={() => setUi({ rowHeight: rowHeight >= 72 ? 32 : rowHeight + 20 })}>
        {rowHeight <= 32 ? "S" : rowHeight <= 52 ? "M" : "L"}
      </Button>
      <div className="flex items-center">
        <IconButton size="sm" title="Zoom out (⌘ scroll)" onClick={() => setUi({ playlistZoom: Math.max(2, playlistZoom / 1.4) })}><Minus size={12} /></IconButton>
        <IconButton size="sm" title="Zoom in (⌘ scroll)" onClick={() => setUi({ playlistZoom: Math.min(400, playlistZoom * 1.4) })}><Plus size={12} /></IconButton>
      </div>
    </div>
  );
}
