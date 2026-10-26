import { useEffect } from "react";
import { BottomDock } from "@/features/dock/BottomDock";
import { Playlist } from "@/features/playlist/Playlist";
import { Sidebar } from "@/features/sidebar/Sidebar";
import { StatusBar } from "@/features/shell/StatusBar";
import { Toasts } from "@/features/shell/Toasts";
import { TopBar } from "@/features/shell/TopBar";
import { Splitter } from "@/features/shell/Splitter";
import { SettingsDialog } from "@/features/settings/SettingsDialog";
import { RecordDialog } from "@/features/record/RecordDialog";
import { useShortcuts } from "@/features/shell/shortcuts";
import { useEngine } from "@/state/engine";
import { setUi, useUi } from "@/state/ui";

export function App() {
  const status = useEngine((s) => s.status);
  const error = useEngine((s) => s.error);
  const hasDoc = useEngine((s) => s.doc !== undefined);
  const { sidebarOpen, sidebarWidth, bottomOpen, bottomHeight } = useUi();
  useShortcuts();
  useDefaultSelection();

  return (
    <div className="flex h-full flex-col">
      <TopBar />
      <div className="flex min-h-0 flex-1 border-t border-line">
        {sidebarOpen && (
          <>
            <div className="flex min-w-0 flex-col bg-panel" style={{ width: sidebarWidth }}>
              <Sidebar />
            </div>
            <Splitter direction="x" onDrag={(d) => setUi({ sidebarWidth: Math.min(520, Math.max(180, sidebarWidth + d)) })} />
          </>
        )}
        <div className="flex min-w-0 flex-1 flex-col">
          {hasDoc ? (
            <>
              <div className="min-h-0 flex-1">
                <Playlist />
              </div>
              {bottomOpen && (
                <>
                  <Splitter direction="y" onDrag={(d) => setUi({ bottomHeight: Math.min(window.innerHeight - 200, Math.max(140, bottomHeight - d)) })} />
                  <div className="min-h-0 bg-panel" style={{ height: bottomHeight }}>
                    <BottomDock />
                  </div>
                </>
              )}
            </>
          ) : (
            <div className="flex flex-1 flex-col items-center justify-center gap-2 text-dim">
              <div className="text-sm">{status === "connecting" ? "Connecting to the engine…" : "No engine"}</div>
              {error && <div className="max-w-md text-center text-xs text-faint">{error}</div>}
            </div>
          )}
        </div>
      </div>
      <StatusBar />
      <Toasts />
      <SettingsDialog />
      <RecordDialog />
    </div>
  );
}

/** Keeps a pattern and channel selected while the song has any. */
function useDefaultSelection() {
  const doc = useEngine((s) => s.doc);
  const { selectedPattern, selectedChannel } = useUi();

  useEffect(() => {
    if (!doc) return;
    if (!doc.patterns.some((p) => p.id === selectedPattern)) setUi({ selectedPattern: doc.patterns[0]?.id });
    if (!doc.channels.some((c) => c.id === selectedChannel)) setUi({ selectedChannel: doc.channels[0]?.id });
  }, [doc, selectedPattern, selectedChannel]);
}
