import { Trash2, X } from "lucide-react";
import { useEffect, useState } from "react";

import { cn } from "@/lib/cn";
import { toast, useEngine } from "@/state/engine";
import { deleteChat, openChat, refreshChats, useAgent, useChats, type ChatSummary } from "./agent";

export function ago(ms: number) {
  const minutes = Math.round((Date.now() - ms) / 60000);
  if (minutes < 1) return "just now";
  if (minutes < 60) return `${minutes} min ago`;
  const hours = Math.round(minutes / 60);
  if (hours < 24) return `${hours} h ago`;
  const days = Math.round(hours / 24);
  if (days < 7) return `${days} d ago`;
  return new Date(ms).toLocaleDateString(undefined, { day: "numeric", month: "short", year: days > 300 ? "numeric" : undefined });
}

/** Every conversation: this song's first, then the other songs'. Any can be reopened here, where
    it carries on with this song; deleting one asks once more. */
export function Conversations({ onOpen }: { onOpen: () => void }) {
  const chats = useChats((s) => s.chats);
  const project = useAgent((s) => s.project);
  const current = useAgent((s) => s.chatId);
  const running = useAgent((s) => s.running);
  const connected = useEngine((s) => s.status === "connected");
  const [confirming, setConfirming] = useState<string>();

  useEffect(() => {
    if (connected) void refreshChats().catch(() => {});
  }, [connected]);

  const here = chats.filter((c) => c.project === project);
  const elsewhere = chats.filter((c) => c.project !== project);

  const row = (c: ChatSummary, showSong: boolean) => (
    <div key={c.id}
      onClick={() => {
        if (running) return;
        void openChat(c.id).then(onOpen, (e) => toast(`Couldn't open the conversation: ${e instanceof Error ? e.message : e}`));
      }}
      className={cn("group flex cursor-default items-start gap-2 rounded-[6px] px-2 py-1.5", c.id === current ? "bg-control" : "hover:bg-raised", running && "opacity-60")}>
      <div className="min-w-0 flex-1">
        <div className="truncate text-xs text-fg">{c.title || "Untitled"}</div>
        <div className="truncate text-2xs text-faint">
          {showSong ? `${c.projectName} · ` : ""}{c.messages} {c.messages === 1 ? "message" : "messages"} · {ago(c.updated)}
        </div>
      </div>
      {confirming === c.id ? (
        <div className="flex shrink-0 items-center gap-1" onClick={(e) => e.stopPropagation()}>
          <button type="button" className="rounded-[4px] bg-rec px-1.5 py-0.5 text-2xs text-black" onClick={() => {
            setConfirming(undefined);
            void deleteChat(c.id);
          }}>Delete</button>
          <button type="button" className="text-faint hover:text-fg" title="Keep it" onClick={() => setConfirming(undefined)}><X size={11} /></button>
        </div>
      ) : (
        <button type="button" title="Delete this conversation" disabled={running && c.id === current}
          onClick={(e) => {
            e.stopPropagation();
            setConfirming(c.id);
          }}
          className="mt-0.5 shrink-0 text-faint opacity-0 hover:text-rec group-hover:opacity-100"><Trash2 size={11} /></button>
      )}
    </div>
  );

  return (
    <div className="h-full overflow-y-auto overscroll-contain px-2 py-2">
      {chats.length === 0 && <div className="px-2 py-6 text-center text-xs text-faint">No conversations yet.</div>}
      {here.length > 0 && (
        <>
          <div className="px-2 pb-1 text-2xs uppercase tracking-wide text-faint">This song</div>
          {here.map((c) => row(c, false))}
        </>
      )}
      {elsewhere.length > 0 && (
        <>
          <div className={cn("px-2 pb-1 text-2xs uppercase tracking-wide text-faint", here.length > 0 && "mt-3")}>Other songs</div>
          {elsewhere.map((c) => row(c, true))}
        </>
      )}
    </div>
  );
}
