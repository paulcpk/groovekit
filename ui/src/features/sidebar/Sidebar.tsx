import { Tabs } from "@/components/controls";
import { AgentPanel } from "@/features/agent/AgentPanel";
import { Browser } from "@/features/browser/Browser";
import { History } from "@/features/history/History";
import { setUi, useUi, type SidebarTab } from "@/state/ui";
import { useEngine } from "@/state/engine";

export function Sidebar() {
  const tab = useUi((s) => s.sidebarTab);
  const agentChanges = useEngine((s) => s.changes.filter((c) => c.origin !== "ui" && Date.now() - c.time < 60000).length);

  return (
    <>
      <div className="flex h-8 shrink-0 items-center border-b border-line px-2">
        <Tabs<SidebarTab>
          value={tab}
          onChange={(t) => setUi({ sidebarTab: t })}
          items={[
            { value: "browser", label: "Browser" },
            { value: "history", label: "History" },
            { value: "activity", label: <span className="flex items-center gap-1">Agent{agentChanges > 0 && <span className="h-1.5 w-1.5 rounded-full bg-agent" />}</span> },
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
