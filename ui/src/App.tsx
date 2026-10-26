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
import { ProjectDialog } from "@/features/shell/ProjectDialog";
import { ExportDialog } from "@/features/shell/ExportDialog";
import { useLiveChannel, useTypingKeyboard } from "@/features/shell/liveInput";
import { useAgentConversation } from "@/features/agent/AgentPanel";
import { useShortcuts } from "@/features/shell/shortcuts";
import { Windows } from "@/features/windows/Windows";
import { useEngine } from "@/state/engine";
import { setUi, useUi } from "@/state/ui";

export function App() {
  const status = useEngine((s) => s.status);
  const error = useEngine((s) => s.error);
  const hasDoc = useEngine((s) => s.doc !== undefined);
  const { sidebarOpen, sidebarWidth, bottomOpen, bottomHeight: chosenHeight, bottomPanel } = useUi();
  // The mixer needs room for its faders.
  const bottomHeight = bottomPanel === "mixer" ? Math.max(chosenHeight, 420) : chosenHeight;
  useShortcuts();
  useDefaultSelection();
  useLiveChannel();
  useAgentConversation();
  useTypingKeyboard();

  return (
    <div className="flex h-full flex-col">
      <TopBar />
      <div className="flex min-h-0 flex-1 border-t border-line">
        {sidebarOpen && (
          <>
            <div className="flex min-w-0 flex-col bg-panel" style={{ width: sidebarWidth }}>
              <Sidebar />
            </div>
            {/* Each step applies to the current size: the handler lives as long as the drag. */}
            <Splitter direction="x" onDrag={(d) => setUi((s) => ({ sidebarWidth: Math.min(520, Math.max(180, s.sidebarWidth + d)) }))} />
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
                  <Splitter direction="y" onDrag={(d) => setUi((s) => {
                    // The mixer shows at least 420 px, so dragging starts from what's shown.
                    const shown = s.bottomPanel === "mixer" ? Math.max(s.bottomHeight, 420) : s.bottomHeight;
                    return { bottomHeight: Math.min(window.innerHeight - 200, Math.max(140, shown - d)) };
                  })} />
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
      <Windows />
      <Toasts />
      <SettingsDialog />
      <RecordDialog />
      <ProjectDialog />
      <ExportDialog />
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
