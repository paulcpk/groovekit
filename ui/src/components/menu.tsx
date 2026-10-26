import { ContextMenu as CM, DropdownMenu as DM } from "radix-ui";
import { cn } from "@/lib/cn";

const content = "z-50 min-w-[180px] rounded-md border border-line bg-raised p-1 text-sm text-fg shadow-xl shadow-black/50";
const item = "flex h-7 cursor-default items-center gap-2 rounded-[4px] px-2 outline-none data-[highlighted]:bg-hover data-[disabled]:opacity-40";

/** Menus render in a portal, but React still bubbles their events to the components that own
    them, so a click on an item would also land on, say, the playlist lane under a clip. */
const ownEvents = {
  onPointerDown: (e: React.SyntheticEvent) => e.stopPropagation(),
  onClick: (e: React.SyntheticEvent) => e.stopPropagation(),
  onDoubleClick: (e: React.SyntheticEvent) => e.stopPropagation(),
};

export const ContextMenu = CM.Root;
export const ContextMenuTrigger = CM.Trigger;

export function ContextMenuContent({ children, className }: { children: React.ReactNode; className?: string }) {
  return (
    <CM.Portal>
      <CM.Content className={cn(content, className)} {...ownEvents}>{children}</CM.Content>
    </CM.Portal>
  );
}

export function ContextMenuItem({ children, onSelect, disabled, danger, shortcut }: {
  children: React.ReactNode; onSelect?: () => void; disabled?: boolean; danger?: boolean; shortcut?: string;
}) {
  return (
    <CM.Item className={cn(item, danger && "text-rec")} onSelect={onSelect} disabled={disabled}>
      <span className="flex-1">{children}</span>
      {shortcut && <span className="text-2xs text-faint">{shortcut}</span>}
    </CM.Item>
  );
}

export function ContextMenuSeparator() {
  return <CM.Separator className="my-1 h-px bg-line" />;
}

export function ContextMenuLabel({ children }: { children: React.ReactNode }) {
  return <CM.Label className="px-2 py-1 text-2xs uppercase tracking-wide text-faint">{children}</CM.Label>;
}

export function ContextMenuSub({ label, children }: { label: React.ReactNode; children: React.ReactNode }) {
  return (
    <CM.Sub>
      <CM.SubTrigger className={cn(item, "data-[state=open]:bg-hover")}>
        <span className="flex-1">{label}</span>
        <span className="text-faint">›</span>
      </CM.SubTrigger>
      <CM.Portal>
        <CM.SubContent className={cn(content, "max-h-[60vh] overflow-y-auto")} {...ownEvents}>{children}</CM.SubContent>
      </CM.Portal>
    </CM.Sub>
  );
}

export const Dropdown = DM.Root;
export const DropdownTrigger = DM.Trigger;

export function DropdownContent({ children, className, align = "start" }: { children: React.ReactNode; className?: string; align?: "start" | "end" | "center" }) {
  return (
    <DM.Portal>
      <DM.Content align={align} sideOffset={4} className={cn(content, "max-h-[70vh] overflow-y-auto", className)} {...ownEvents}>{children}</DM.Content>
    </DM.Portal>
  );
}

export function DropdownItem({ children, onSelect, disabled, checked, danger }: {
  children: React.ReactNode; onSelect?: () => void; disabled?: boolean; checked?: boolean; danger?: boolean;
}) {
  return (
    <DM.Item className={cn(item, danger && "text-rec")} onSelect={onSelect} disabled={disabled}>
      {checked !== undefined && <span className="w-3 text-accent">{checked ? "✓" : ""}</span>}
      <span className="flex-1">{children}</span>
    </DM.Item>
  );
}

export function DropdownSeparator() {
  return <DM.Separator className="my-1 h-px bg-line" />;
}

export function DropdownLabel({ children }: { children: React.ReactNode }) {
  return <DM.Label className="px-2 py-1 text-2xs uppercase tracking-wide text-faint">{children}</DM.Label>;
}

export function DropdownSub({ label, children }: { label: React.ReactNode; children: React.ReactNode }) {
  return (
    <DM.Sub>
      <DM.SubTrigger className={cn(item, "data-[state=open]:bg-hover")}>
        <span className="flex-1">{label}</span>
        <span className="text-faint">›</span>
      </DM.SubTrigger>
      <DM.Portal>
        <DM.SubContent className={cn(content, "max-h-[60vh] overflow-y-auto")}>{children}</DM.SubContent>
      </DM.Portal>
    </DM.Sub>
  );
}
