import { AudioLines, ChevronDown } from "lucide-react";

import { Dropdown, DropdownContent, DropdownItem, DropdownLabel, DropdownSeparator, DropdownTrigger } from "@/components/menu";

import { cn } from "@/lib/cn";

import { inDesktopApp } from "@/engine/client";
import { pickFolder } from "@/lib/files";

import { addStyle, needsHearing, refreshReferences, useListening } from "./listening";

import { hearLabel } from "./ListeningPanel";

/** Ranks the browser by fit to one of the user's styles, or adds one: the folders in the
    references folder GrooveKit hasn't heard (or that changed) first, or any folder. */
export function StylePicker({ style, styles, onChange }: { style?: string; styles: string[]; onChange: (name?: string) => void }) {
  const { references, status } = useListening();
  const running = !!status?.styles.running;
  const toHear = references?.folders.filter(needsHearing) ?? [];

  const add = () => {
    if (!inDesktopApp) return;
    void pickFolder("Choose a folder of reference tracks", references?.exists ? references.folder : undefined).then((path) => {
      if (path) void addStyle(path).then((r) => onChange(r.name));
    });
  };

  return (
    <Dropdown onOpenChange={(open) => open && void refreshReferences().catch(() => {})}>
      <DropdownTrigger asChild>
        <button type="button" title="Rank samples by how well they fit a style: a folder of reference tracks GrooveKit has listened to"
          className={cn("flex h-5 shrink-0 items-center gap-1 rounded-[3px] px-1.5 text-2xs", style ? "bg-agent text-black" : "bg-control text-dim hover:text-fg")}>
          <AudioLines size={9} /> {style ?? "Style"} <ChevronDown size={9} />
        </button>
      </DropdownTrigger>
      <DropdownContent>
        <DropdownLabel>Rank by fit to</DropdownLabel>
        {styles.map((name) => (
          <DropdownItem key={name} checked={name === style} onSelect={() => onChange(name)}>{name}</DropdownItem>
        ))}
        {styles.length === 0 && <DropdownItem disabled>No styles yet</DropdownItem>}
        {style && <DropdownItem onSelect={() => onChange(undefined)}>No style</DropdownItem>}
        {toHear.length > 0 && (
          <>
            <DropdownSeparator />
            <DropdownLabel>In your references folder</DropdownLabel>
            {toHear.map((f) => (
              <DropdownItem key={f.path} disabled={running} onSelect={() => void addStyle(f.path)}>
                {hearLabel(f)}
              </DropdownItem>
            ))}
          </>
        )}
        <DropdownSeparator />
        <DropdownItem disabled={!inDesktopApp} onSelect={add}>Add a style from a folder of tracks…</DropdownItem>
      </DropdownContent>
    </Dropdown>
  );
}
