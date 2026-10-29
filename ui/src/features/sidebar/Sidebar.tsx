import { Tabs } from "@/components/controls";
import { AgentPanel, RecordingDot } from "@/features/agent/AgentPanel";
import { useAgentView } from "@/features/agent/maps";
import { useRecording } from "@/features/agent/voice";
import { Browser } from "@/features/browser/Browser";
import { History } from "@/features/history/History";
import { setUi, useUi, type SidebarTab } from "@/state/ui";
import { useEngine } from "@/state/engine";

export function Sidebar() {
  const tab = useUi((s) => s.sidebarTab);
  const recording = useRecording();
  const agentChanges = useEngine((s) => s.changes.filter((c) => c.origin !== "ui" && Date.now() - c.time < 60000).length);

  return (
    <>
      <div className="flex h-8 shrink-0 items-center border-b border-line px-2" data-tour="sidebar">
        <Tabs<SidebarTab>
          value={tab}
          onChange={(t) => {
            setUi({ sidebarTab: t });
            // Back to the recording.
            if (t === "activity" && recording) useAgentView.setState({ view: "chat" });
          }}
          items={[
            { value: "browser", label: "Browser" },
            { value: "history", label: "History" },
            { value: "activity", label: <span className="flex items-center gap-1" data-tour="agent">Agent{recording && tab !== "activity" ? <RecordingDot /> : agentChanges > 0 && <span className="h-1.5 w-1.5 rounded-full bg-agent" />}</span>,
              title: recording && tab !== "activity" ? "A voice message is being recorded" : undefined },
          ]}
        />
      </div>
      <div className="min-h-0 flex-1">
        {tab === "browser" && <Browser />}
        {tab === "history" && <History />}
        {tab === "activity" && <AgentPanel />}
      </div>
    </>
  );
}
