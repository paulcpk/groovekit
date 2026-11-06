import { useEffect } from "react";
import { useShallow } from "zustand/react/shallow";
import { BottomDock } from "@/features/dock/BottomDock";
import { Playlist } from "@/features/playlist/Playlist";
import { Sidebar } from "@/features/sidebar/Sidebar";
import { StatusBar } from "@/features/shell/StatusBar";
import { Toasts } from "@/features/shell/Toasts";
import { ControlMenu } from "@/components/controlMenu";
import { TopBar } from "@/features/shell/TopBar";
import { Splitter } from "@/features/shell/Splitter";
import { SettingsDialog } from "@/features/settings/SettingsDialog";
import { RecordDialog } from "@/features/record/RecordDialog";
import { ProjectDialog } from "@/features/shell/ProjectDialog";
import { SaveChangesDialog, useQuitGuard } from "@/features/shell/SaveChanges";
import { ExportDialog } from "@/features/shell/ExportDialog";
import { useLiveChannel, useTypingKeyboard } from "@/features/shell/liveInput";
import { useMachineFocus } from "@/features/rack/machineFocus";
import { useAgentConversation } from "@/features/agent/AgentPanel";
import { useShortcuts } from "@/features/shell/shortcuts";
import { ReferencesPrompt } from "@/features/browser/ReferencesPrompt";
import { Welcome } from "@/features/shell/Welcome";
import { EngineVersionBanner } from "@/features/shell/EngineVersionBanner";
import { Windows } from "@/features/windows/Windows";
import { Spotlight } from "@/features/spotlight/Spotlight";
import { releaseSpotlight, summonSpotlight } from "@/features/spotlight/spotlightState";
import { createAutomation } from "@/features/playlist/automation";
import { openOverview } from "@/features/windows/overviewState";
import { anythingInFront, bringToFront, followPluginWindows } from "@/features/windows/arrange";
import { commitSwitcher, stepSwitcher } from "@/features/windows/switcherState";
import { syncSuggestions } from "@/features/suggestions/suggestions";
import { listen, useEngine } from "@/state/engine";
import { setUi, useUi } from "@/state/ui";

export function App() {
  const status = useEngine((s) => s.status);
  const error = useEngine((s) => s.error);
  const hasDoc = useEngine((s) => s.doc !== undefined);
  const { sidebarOpen, sidebarWidth, bottomOpen, bottomHeight: chosenHeight, bottomPanel } = useUi(useShallow((s) =>
    ({ sidebarOpen: s.sidebarOpen, sidebarWidth: s.sidebarWidth, bottomOpen: s.bottomOpen, bottomHeight: s.bottomHeight, bottomPanel: s.bottomPanel })));
  // The mixer needs room for its faders.
  const bottomHeight = bottomPanel === "mixer" ? Math.max(chosenHeight, 420) : chosenHeight;

  return (
    <div className="flex h-full flex-col">
      <AppHooks />
      <TopBar />
      <EngineVersionBanner />
      {/* The app's own window: a click in it sends the windows over it to the background (the top
          bar's transport stays usable with a plugin in front). A click, not a press, so dragging a
          sample from the browser into a sampler's window still works; not a double-click's second,
          which would send away the window its first just opened. */}
      <div className="flex min-h-0 flex-1 border-t border-line" onClickCapture={(e) => e.detail < 2 && anythingInFront() && bringToFront("main")}>
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
      <Spotlight />
      <ControlMenu />
      <Toasts />
      <SettingsDialog />
      <RecordDialog />
      <ProjectDialog />
      <SaveChangesDialog />
      <ExportDialog />
      <ReferencesPrompt />
      <Welcome />
    </div>
  );
}

/** The app's behaviour that isn't drawn: shortcuts, the keyboards, the agent's conversation. In
    a component of its own, so what these watch (the selected channel, say) re-renders nothing
    but it, not the whole app. */
function AppHooks() {
  useShortcuts();
  useDefaultSelection();
  useLiveChannel();
  useMachineFocus();
  useAgentConversation();
  useTypingKeyboard();
  useQuitGuard();
  // "Automate" in the bar above a plugin's window asks the UI for an automation clip.
  // Sounds the engine picks out for the song in the background.
  useEffect(() => syncSuggestions(), []);
  useEffect(() => listen("createAutomation", (params) => {
    const doc = useEngine.getState().doc;
    if (doc) void createAutomation(doc, params.target, params.value);
  }), []);
  // The plugins' windows (the engine's): which are open, and Cmd+Up, Cmd+Left/Right, Cmd+K typed in them.
  useEffect(() => {
    const stops = [
      followPluginWindows(),
      listen("windowsOverview", () => void openOverview()),
      listen("windowsSwitch", ({ direction }: { direction: 1 | -1 }) => stepSwitcher(direction > 0 ? 1 : -1)),
      listen("windowsSwitchEnd", () => commitSwitcher()),
      // Cmd+K in a plugin's window: the command box, listening while Cmd is held there.
      listen("spotlight", ({ held }: { held: boolean }) => (held ? summonSpotlight() : releaseSpotlight())),
    ];
    return () => stops.forEach((stop) => stop());
  }, []);
  return null;
}

/** Keeps a pattern and channel selected while the song has any. Watches the stores directly, so
    a change to the song doesn't render the whole app. */
function useDefaultSelection() {
  useEffect(() => {
    const check = () => {
      const doc = useEngine.getState().doc;
      if (!doc) return;
      const { selectedPattern, selectedChannel } = useUi.getState();
      if (!doc.patterns.some((p) => p.id === selectedPattern)) setUi({ selectedPattern: doc.patterns[0]?.id });
      if (!doc.channels.some((c) => c.id === selectedChannel)) setUi({ selectedChannel: doc.channels[0]?.id });
    };
    check();
    const unsubscribeEngine = useEngine.subscribe((s, previous) => s.doc !== previous.doc && check());
    const unsubscribeUi = useUi.subscribe((s, previous) => (s.selectedPattern !== previous.selectedPattern || s.selectedChannel !== previous.selectedChannel) && check());
    return () => {
      unsubscribeEngine();
      unsubscribeUi();
    };
  }, []);
}
