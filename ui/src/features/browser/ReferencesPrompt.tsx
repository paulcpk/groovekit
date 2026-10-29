import { FolderPlus } from "lucide-react";
import { useEffect, useState } from "react";
import { Button } from "@/components/controls";
import { Dialog } from "@/components/dialog";
import { inDesktopApp } from "@/engine/client";
import { isWelcomed, useTour } from "@/features/shell/Welcome";
import { toast, useEngine } from "@/state/engine";
import { createReferences, refreshReferences } from "./listening";

const ASKED = "groovekit.referencesAsked";

const home = (path: string) => path.replace(/^\/Users\/[^/]+/, "~");

/** Asks once, on the first start, to create the folder for reference tracks, if it isn't there;
    after the welcome and its tour, when those are showing. */
export function ReferencesPrompt() {
  const connected = useEngine((s) => s.status === "connected");
  const touring = useTour((s) => s.open);
  const [folder, setFolder] = useState<string>();

  useEffect(() => {
    if (!connected || !inDesktopApp || touring || !isWelcomed()) return;
    try {
      if (localStorage.getItem(ASKED)) return;
    } catch {
      return;
    }
    void refreshReferences().then((r) => !r.exists && setFolder(r.folder), () => {});
  }, [connected, touring]);

  const close = () => {
    setFolder(undefined);
    try {
      localStorage.setItem(ASKED, "1");
    } catch {
      // Without storage it asks again next time.
    }
  };

  return (
    <Dialog open={!!folder} onOpenChange={(open) => !open && close()} title="A folder for your reference tracks">
      <div className="flex flex-col gap-3 text-xs leading-relaxed text-dim">
        <p>
          GrooveKit learns what a style should sound like from tracks you love: it listens to them and compares your songs and
          samples with them. Keep them in one folder, with a folder inside it for each style, named like the style
          (Berlin Techno, Hard Techno…), and GrooveKit finds them by itself.
        </p>
        <div className="flex items-center gap-2 rounded-[6px] bg-bg px-2.5 py-2 font-mono text-[11px] text-fg ring-1 ring-line">
          <FolderPlus size={12} className="text-agent" /> {folder && home(folder)}
        </div>
        <div className="flex justify-end gap-2">
          <Button size="sm" onClick={close}>Not now</Button>
          <Button size="sm" className="bg-agent text-black"
            onClick={() => void createReferences().then(() => { toast(`Created ${home(folder ?? "")}`); close(); }, () => {})}>
            Create the folder
          </Button>
        </div>
      </div>
    </Dialog>
  );
}
