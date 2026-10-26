import type { AutomationTarget } from "@groovekit/schema/project";
import { ContextMenu, ContextMenuContent, ContextMenuItem, ContextMenuSeparator, ContextMenuTrigger } from "@/components/menu";
import { useEngine } from "@/state/engine";
import { createAutomation } from "./automation";

/** A control with a right-click menu that makes an automation clip for what it sets, starting
    from its current value, plus any items of its own. */
export function Automatable({ target, value, children, items, className }: {
  target: AutomationTarget; value: number; children: React.ReactNode; items?: React.ReactNode; className?: string;
}) {
  return (
    <ContextMenu>
      <ContextMenuTrigger asChild><div className={className}>{children}</div></ContextMenuTrigger>
      <ContextMenuContent>
        {items}
        {items && <ContextMenuSeparator />}
        <ContextMenuItem onSelect={() => {
          const doc = useEngine.getState().doc;
          if (doc) void createAutomation(doc, target, value);
        }}>Create automation clip</ContextMenuItem>
      </ContextMenuContent>
    </ContextMenu>
  );
}
