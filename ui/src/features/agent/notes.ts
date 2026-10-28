// The user's notes for the agent: standing wishes ("prefer loops over one-shots") kept by the
// engine for every song or for one, which the agent reads before it works. "notes" notifications
// keep the list current when the agent (or Claude Code) changes them.

import { useEffect } from "react";
import { create } from "zustand";
import { call, engine, listen, useEngine } from "@/state/engine";

export interface AgentNote {
  id: string;
  text: string;
  enabled: boolean;
  /** The song folder it's for; absent: every song. */
  project?: string;
  projectName?: string;
  /** Whether it applies to the open song. */
  here: boolean;
  created: number;
  updated: number;
}

export type NoteScope = "all" | "song";

export const useNotes = create<{ notes: AgentNote[]; loaded: boolean }>(() => ({ notes: [], loaded: false }));

listen("notes", (params: { notes: AgentNote[] }) => useNotes.setState({ notes: params.notes, loaded: true }));

export async function refreshNotes() {
  const r = await engine.call<{ notes: AgentNote[] }>("notes.list");
  useNotes.setState({ notes: r.notes, loaded: true });
  return r.notes;
}

/** The notes, fetched when the engine connects and again when another song opens ("here" changes). */
export function useAgentNotes() {
  const connected = useEngine((s) => s.status === "connected");
  const project = useEngine((s) => s.project);

  useEffect(() => {
    if (connected) void refreshNotes().catch(() => {});
  }, [connected, project]);

  return useNotes();
}

export const addNote = (text: string, scope: NoteScope) => call<AgentNote>("notes.add", { text, scope });

export const updateNote = (id: string, change: { text?: string; enabled?: boolean; scope?: NoteScope }) => call<AgentNote>("notes.update", { id, ...change });

export const deleteNote = (id: string) => call<{ deleted: boolean }>("notes.delete", { id });

/** The notes the agent follows in the open song, as a part of its instructions; empty without any. */
export async function notesForAgent() {
  let notes: AgentNote[];
  try {
    notes = await refreshNotes();
  } catch {
    notes = useNotes.getState().notes;
  }
  const applying = notes.filter((n) => n.enabled && n.here);
  if (!applying.length) return "";
  const lines = applying.map((n) => `- ${n.text.replace(/\n+/g, "\n  ")}${n.project ? " (for this song)" : ""}`);
  return `The user's notes for you, from the Notes tab. Follow them unless the user asks otherwise in this conversation:\n${lines.join("\n")}`;
}
