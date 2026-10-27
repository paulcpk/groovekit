import { useEffect } from "react";
import { create } from "zustand";
import { Button } from "@/components/controls";
import { Dialog } from "@/components/dialog";
import { inDesktopApp } from "@/engine/client";
import { call, hasUnsavedChanges, useEngine } from "@/state/engine";

type Reason = "quit" | "open";

const useAsking = create<{ reason?: Reason; answer?: (choice: "save" | "discard" | "cancel") => void }>(() => ({}));

/** Before leaving the song (quitting, or opening another project): with unsaved changes, asks
    whether to save them. Not saving goes back to where the song was saved; the changes since
    stay in the history. Resolves to whether to go on. */
export async function confirmUnsaved(reason: Reason): Promise<boolean> {
  if (!hasUnsavedChanges()) return true;
  if (useAsking.getState().answer) return false;

  const choice = await new Promise<"save" | "discard" | "cancel">((answer) => useAsking.setState({ reason, answer }));
  useAsking.setState({ reason: undefined, answer: undefined });

  try {
    if (choice === "save") await call("project.save");
    if (choice === "discard") await call("project.revert");
  } catch {
    return false;
  }
  return choice !== "cancel";
}

/** In the desktop app, quitting (⌘Q or closing the window) asks the shell's question here. */
export function useQuitGuard() {
  useEffect(() => {
    if (!inDesktopApp) return;
    let stop: (() => void) | undefined;
    let cancelled = false;

    void (async () => {
      const [{ listen }, { invoke }] = await Promise.all([import("@tauri-apps/api/event"), import("@tauri-apps/api/core")]);
      const unlisten = await listen("quit-requested", () => {
        void invoke("quit_acknowledged");
        void confirmUnsaved("quit").then((go) => go && invoke("quit"));
      });
      if (cancelled) unlisten();
      else stop = unlisten;
    })();

    return () => {
      cancelled = true;
      stop?.();
    };
  }, []);
}

export function SaveChangesDialog() {
  const { reason, answer } = useAsking();
  const name = useEngine((s) => s.doc?.name || s.project?.split("/").pop() || "Untitled");

  return (
    <Dialog open={!!answer} onOpenChange={(open) => !open && answer?.("cancel")} title={`Save changes to “${name}”?`} className="w-[400px]">
      <div className="text-xs leading-relaxed text-dim">
        {reason === "quit" ? "GrooveKit is about to quit." : "Another project is about to open."}{" "}
        If you don't save, the song goes back to how it was when you last saved it. The changes since stay in its history, so redo brings them back.
      </div>
      <div className="mt-4 flex items-center gap-2">
        <Button size="sm" onClick={() => answer?.("discard")}>Don't save</Button>
        <div className="flex-1" />
        <Button size="sm" onClick={() => answer?.("cancel")}>Cancel</Button>
        <Button size="sm" active tone="accent" data-autofocus onClick={() => answer?.("save")}>Save</Button>
      </div>
    </Dialog>
  );
}
