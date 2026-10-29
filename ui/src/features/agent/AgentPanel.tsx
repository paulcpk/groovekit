import { History, SquarePen } from "lucide-react";
import { useEffect } from "react";

import { IconButton, Tabs } from "@/components/controls";

import { Activity } from "@/features/history/Activity";
import { cn } from "@/lib/cn";
import { useEngine } from "@/state/engine";
import { loadConversation, newChat, useAgent } from "./agent";

import { NotesView } from "./NotesView";
import { StylesView } from "./StylesView";
import { MapsView } from "./MapsView";
import { useAgentView, type AgentView } from "./maps";
import { useRecording } from "./voice";
import { Chat } from "./Chat";

import { Conversations } from "./Conversations";

export function AgentPanel() {
  const view = useAgentView((s) => s.view);
  const recording = useRecording();
  const setView = (v: AgentView) => useAgentView.setState({ view: v });
  const items = useAgent((s) => s.items);
  const running = useAgent((s) => s.running);

  return (
    <div className="flex h-full flex-col">
      <div className="flex h-8 shrink-0 items-center gap-1 border-b border-line px-2">
        <Tabs<Exclude<AgentView, "chats">> value={view === "chats" ? "chat" : view} onChange={setView}
          items={[{ value: "chat", label: <span className="flex items-center gap-1">Chat{recording && view !== "chat" && <RecordingDot />}</span>,
            title: recording && view !== "chat" ? "A voice message is being recorded" : undefined }, { value: "notes", label: "Notes", title: "What the agent should always keep in mind" },
            { value: "styles", label: "Styles", title: "How the agent makes each style" },
            { value: "maps", label: "Maps", title: "Your reference tracks, heard into song maps" }, { value: "changes", label: "Changes" }]} />
        <div className="flex-1" />
        {(view === "chat" || view === "chats") && (
          <div className="flex shrink-0 items-center gap-1">
            <IconButton size="sm" title="Conversations" onClick={() => setView(view === "chats" ? "chat" : "chats")}
              className={cn(view === "chats" && "bg-control text-fg")}><History size={12} /></IconButton>
            <IconButton size="sm" title="New conversation" disabled={running || (items.length === 0 && view === "chat")}
              onClick={() => {
                newChat();
                setView("chat");
              }}><SquarePen size={12} /></IconButton>
          </div>
        )}
      </div>
      <div className="min-h-0 flex-1">
        {view === "chat" ? <Chat /> : view === "chats" ? <Conversations onOpen={() => setView("chat")} /> : view === "notes" ? <NotesView /> : view === "styles" ? <StylesView /> : view === "maps" ? <MapsView /> : <Activity />}
      </div>
    </div>
  );
}

/** A voice message is being recorded (shown where the message box isn't). */
export function RecordingDot() {
  return <span className="h-1.5 w-1.5 shrink-0 animate-pulse rounded-full bg-rec shadow-[0_0_6px_var(--color-rec)]" />;
}

/** Loads the open project's conversation. */
export function useAgentConversation() {
  const project = useEngine((s) => s.project);
  useEffect(() => {
    void loadConversation(project);
  }, [project]);
}
