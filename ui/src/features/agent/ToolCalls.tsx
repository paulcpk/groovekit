import { AlertTriangle, Check, ChevronRight, Loader2, X } from "lucide-react";
import { useState } from "react";

import { cn } from "@/lib/cn";

import { type ChatItem } from "./agent";

import { Message } from "./Message";

export const TOOL_LABELS: Record<string, string> = {
  get_summary: "Read the song",
  get_project: "Read the song's details",
  get_schema: "Read the song format",
  edit_project: "Edit the song",
  search_samples: "Search samples",
  describe_sample: "Listen to a sample",
  render: "Render and measure",
  analyze_mix: "Analyse the mix",
  analyze_audio: "Analyse audio",
  master: "Master",
  transport: "Transport",
  undo: "Undo",
  redo: "Redo",
  get_history: "Read the history",
  plugins: "Plugins",
  describe_plugin: "Read a plugin",
  record: "Record",
  list_effects: "List effects",
  sample_library: "Sample library",
  project: "Project",
  notes: "Notes",
  style_guide: "Style guide",
  melody: "Hear a melody",
};

/** One line about what a tool call was for. */
export function toolSummary(input: unknown) {
  if (!input || typeof input !== "object") return "";
  const i = input as Record<string, unknown>;
  const value = i.label ?? i.query ?? i.action ?? i.path ?? i.category;
  return typeof value === "string" ? value.replace(/^.*\//, "") : "";
}

export type ToolItem = Extract<ChatItem, { kind: "tool" }>;
/** A run of tool calls: what's happening now while it works, a count once it's done, and each
    call's input and result when opened. */
export function ToolGroup({ tools }: { tools: ToolItem[] }) {
  const [open, setOpen] = useState(false);
  const current = tools.find((t) => t.status === "running");
  const failed = tools.filter((t) => t.status === "error").length;
  const label = (t: ToolItem) => TOOL_LABELS[t.name] ?? t.name;

  return (
    <div className="rounded-[8px] bg-raised/60 ring-1 ring-line">
      <button type="button" onClick={() => setOpen(!open)} className="flex w-full items-center gap-2 rounded-[8px] px-2.5 py-1.5 text-left text-[11px]">
        {current ? <Loader2 size={12} className="shrink-0 animate-spin text-agent" />
          : failed ? <AlertTriangle size={12} className="shrink-0 text-accent" /> : <Check size={12} className="shrink-0 text-ok" />}
        {current ? (
          <span className="text-shimmer min-w-0 flex-1 truncate">{label(current)}{toolSummary(current.input) ? ` · ${toolSummary(current.input)}` : ""}</span>
        ) : (
          <span className="min-w-0 flex-1 truncate text-dim">
            {tools.length === 1 ? label(tools[0]) : `${tools.length} steps`}
            {tools.length === 1 && toolSummary(tools[0].input) && <span className="text-faint"> · {toolSummary(tools[0].input)}</span>}
            {failed > 0 && <span className="text-accent"> · {failed} didn't work</span>}
          </span>
        )}
        <ChevronRight size={11} className={cn("shrink-0 text-faint transition-transform", open && "rotate-90")} />
      </button>
      {open && (
        <div className="flex flex-col border-t border-line py-1">
          {tools.map((t) => <ToolRow key={t.id} tool={t} />)}
        </div>
      )}
    </div>
  );
}

export function ToolRow({ tool }: { tool: ToolItem }) {
  const [open, setOpen] = useState(false);
  const summary = toolSummary(tool.input);

  return (
    <div>
      <button type="button" onClick={() => setOpen(!open)} className="flex w-full items-center gap-1.5 px-2.5 py-1 text-left text-[11px] hover:bg-hover/50">
        {tool.status === "running" ? <Loader2 size={10} className="shrink-0 animate-spin text-agent" />
          : tool.status === "done" ? <Check size={10} className="shrink-0 text-ok" /> : <X size={10} className="shrink-0 text-rec" />}
        <span className="shrink-0 text-fg">{TOOL_LABELS[tool.name] ?? tool.name}</span>
        {summary && <span className="min-w-0 flex-1 truncate text-faint">{summary}</span>}
      </button>
      {open && (
        <div className="mx-2.5 mb-1.5 rounded-[6px] bg-bg px-2 py-1.5 font-mono text-[10px] leading-snug text-dim">
          <Message text={JSON.stringify(tool.input, null, 1)}>
            <pre className="max-h-40 overflow-auto whitespace-pre-wrap break-all">{JSON.stringify(tool.input, null, 1)}</pre>
          </Message>
          {tool.result !== undefined && (
            <Message text={tool.result}>
              <pre className={cn("mt-1.5 max-h-48 overflow-auto border-t border-line/60 pt-1.5 whitespace-pre-wrap break-all", tool.status === "error" && "text-rec")}>
                {tool.result.length > 4000 ? `${tool.result.slice(0, 4000)}…` : tool.result}
              </pre>
            </Message>
          )}
        </div>
      )}
    </div>
  );
}
