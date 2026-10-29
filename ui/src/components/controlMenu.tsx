import type { AutomationTarget } from "@groovekit/schema/project";
import { create } from "zustand";
import { Dropdown, DropdownContent, DropdownItem, DropdownSeparator, DropdownTrigger } from "@/components/menu";
import { createAutomation } from "@/features/playlist/automation";
import { useEngine } from "@/state/engine";

/** What a control tells the menu when it's right-clicked. */
export interface ControlMenuSpec {
  /** What the control sets, when automation can drive it. */
  automation?: AutomationTarget;
  /** The value the automation clip starts from, in the parameter's own units. */
  value: number;
  /** What "Reset" goes back to, in the control's units; none hides the item. */
  defaultValue?: number;
  format?: (value: number) => string;
  onCommit?: (value: number) => void;
  /** Items of the control's own, above the common ones. */
  items?: React.ReactNode;
}

const useControlMenu = create<{ spec?: ControlMenuSpec; x: number; y: number }>(() => ({ x: 0, y: 0 }));

/** Opens the app's control menu for a right-click on a control. The event stops here, so no
    menu around the control opens as well. */
export function openControlMenu(e: React.MouseEvent, spec: ControlMenuSpec) {
  e.preventDefault();
  e.stopPropagation();
  // A control with nothing to offer (no default, nothing to automate) opens no menu.
  if (spec.defaultValue === undefined && !spec.automation && !spec.items) return;
  useControlMenu.setState({ spec, x: e.clientX, y: e.clientY });
}

/** The one menu every knob, fader and draggable number shares: reset to the default, and make
    an automation clip for what it sets. Mounted once, by the app. */
export function ControlMenu() {
  const { spec, x, y } = useControlMenu();
  const close = () => useControlMenu.setState({ spec: undefined });
  const reset = spec?.defaultValue !== undefined && spec.onCommit ? spec : undefined;
  const text = reset ? (reset.format ? reset.format(reset.defaultValue!) : String(reset.defaultValue)) : "";

  return (
    <Dropdown open={spec !== undefined} onOpenChange={(open) => !open && close()}>
      <DropdownTrigger asChild>
        <span className="pointer-events-none fixed h-0 w-0" style={{ left: x, top: y }} />
      </DropdownTrigger>
      {spec && (
        <DropdownContent>
          {spec.items}
          {spec.items && (reset || spec.automation) && <DropdownSeparator />}
          {reset && <DropdownItem onSelect={() => reset.onCommit!(reset.defaultValue!)}>Reset to {text}</DropdownItem>}
          {spec.automation && (
            <DropdownItem onSelect={() => {
              const doc = useEngine.getState().doc;
              if (doc) void createAutomation(doc, spec.automation!, spec.value);
            }}>Create automation clip</DropdownItem>
          )}
        </DropdownContent>
      )}
    </Dropdown>
  );
}
