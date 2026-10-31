import { useEffect } from "react";
import { engine, useEngine } from "@/state/engine";
import { useUi } from "@/state/ui";

/** The machines' panels (Groove-8, Groove-9, Groove-3) show and edit the pattern selected here: the
    engine is told whenever the selection changes, and again after it connects or opens a project. */
export function useMachineFocus() {
  const connected = useEngine((s) => s.status === "connected");
  const project = useEngine((s) => s.project);
  const pattern = useUi((s) => s.selectedPattern);

  useEffect(() => {
    if (connected) void engine.call("machines.focus", { pattern: pattern ?? null }).catch(() => {});
  }, [connected, project, pattern]);
}
