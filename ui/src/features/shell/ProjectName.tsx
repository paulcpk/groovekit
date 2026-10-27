import { useState } from "react";
import { execute, useEngine } from "@/state/engine";

export function ProjectName() {
  const name = useEngine((s) => s.doc?.name);
  const folder = useEngine((s) => s.project);
  const unsaved = useEngine((s) => s.saved !== undefined && s.saved !== s.head);
  const [editing, setEditing] = useState(false);
  const shown = name || folder?.split("/").pop() || "Untitled";

  if (editing) {
    return (
      <input
        autoFocus
        defaultValue={shown}
        className="h-6 w-40 rounded-[3px] bg-bg px-1.5 text-sm text-fg outline-none ring-1 ring-accent"
        onBlur={(e) => {
          setEditing(false);
          const v = e.target.value.trim();
          if (v && v !== name) void execute({ type: "renameProject", name: v });
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
    <div className="flex min-w-0 items-center gap-1.5">
      <div className="max-w-[200px] cursor-text truncate text-sm text-dim hover:text-fg" title={folder} onDoubleClick={() => setEditing(true)}>
        {shown}
      </div>
      {unsaved && <span className="h-1.5 w-1.5 shrink-0 rounded-full bg-dim" title="Unsaved changes (⌘S saves)" />}
    </div>
  );
}
