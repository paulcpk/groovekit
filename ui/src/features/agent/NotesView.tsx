import { NotebookPen, Pencil, Trash2, X } from "lucide-react";
import { useLayoutEffect, useRef, useState } from "react";
import { cn } from "@/lib/cn";
import { useEngine } from "@/state/engine";
import { addNote, deleteNote, updateNote, useAgentNotes, type AgentNote, type NoteScope } from "./notes";

const EXAMPLES = [
  "Prefer loops over one-shots for drums",
  "Techno should sound hard and industrial: around 140 BPM, a distorted rumbling kick, sparse percussion",
  "Leave my Digitakt recordings as they are; only add parts around them",
  "Master to −9 LUFS for club play",
];

/** The user's notes for the agent: what it should always keep in mind, for every song or this one.
    Each can be switched off, edited or deleted; notes for other songs are listed apart. */
export function NotesView() {
  const { notes, loaded } = useAgentNotes();
  const connected = useEngine((s) => s.status === "connected");
  const [draft, setDraft] = useState("");
  const [scope, setScope] = useState<NoteScope>("all");

  const everywhere = notes.filter((n) => !n.project);
  const here = notes.filter((n) => n.project && n.here);
  const elsewhere = notes.filter((n) => !n.here);

  const submit = () => {
    const text = draft.trim();
    if (!text || !connected) return;
    void addNote(text, scope).then(() => setDraft(""), () => {});
  };

  return (
    <div className="flex h-full flex-col">
      <div className="min-h-0 flex-1 overflow-y-auto overscroll-contain px-2 py-2">
        {loaded && notes.length === 0 ? (
          <div className="flex animate-rise flex-col gap-3 px-1 pt-1">
            <div className="flex items-center gap-2 text-sm text-fg"><NotebookPen size={14} className="text-agent" /> Notes for the agent</div>
            <p className="text-xs leading-relaxed text-dim">
              Tell the agent what to keep in mind every time: how you like to work, what a style should sound like. It reads these
              before it starts, in every song or just in this one. Claude Code sees them too.
            </p>
            <div className="flex flex-col gap-1.5">
              {EXAMPLES.map((e) => (
                <button key={e} type="button" disabled={!connected} onClick={() => setDraft(e)}
                  className="rounded-[8px] bg-raised px-2.5 py-2 text-left text-xs text-dim ring-1 ring-line transition-colors hover:text-fg hover:ring-agent/50">
                  {e}
                </button>
              ))}
            </div>
          </div>
        ) : (
          <>
            <Section title="Every song" notes={everywhere} first />
            <Section title="This song" notes={here} first={everywhere.length === 0} />
            <Section title="Other songs" notes={elsewhere} first={everywhere.length + here.length === 0} />
          </>
        )}
      </div>
      <div className="shrink-0 p-2 pt-1">
        <div className="rounded-[12px] bg-raised ring-1 ring-line transition-[box-shadow,ring-color] duration-150 focus-within:ring-agent/50">
          <NoteField value={draft} onChange={setDraft} onSubmit={submit} disabled={!connected}
            placeholder={connected ? "Add a note for the agent…" : "Waiting for the engine…"} />
          <div className="flex items-center gap-1 px-1.5 pb-1.5">
            <ScopeSwitch value={scope} onChange={setScope} />
            <div className="flex-1" />
            <button type="button" disabled={!draft.trim() || !connected} onClick={submit}
              className={cn("h-7 rounded-full px-3 text-xs transition-colors", draft.trim() && connected ? "bg-agent text-black" : "bg-control text-faint")}>
              Add
            </button>
          </div>
        </div>
      </div>
    </div>
  );
}

function Section({ title, notes, first }: { title: string; notes: AgentNote[]; first?: boolean }) {
  if (!notes.length) return null;
  return (
    <>
      <div className={cn("px-1 pb-1 text-2xs uppercase tracking-wide text-faint", !first && "mt-3")}>{title}</div>
      <div className="flex flex-col gap-1.5">
        {notes.map((n) => <NoteCard key={n.id} note={n} />)}
      </div>
    </>
  );
}

function NoteCard({ note }: { note: AgentNote }) {
  const [editing, setEditing] = useState(false);
  const [text, setText] = useState(note.text);
  const [scope, setScope] = useState<NoteScope>(note.project ? "song" : "all");
  const [confirming, setConfirming] = useState(false);
  const elsewhere = !note.here;

  const startEditing = () => {
    setText(note.text);
    setScope(note.project ? "song" : "all");
    setEditing(true);
  };

  const save = () => {
    const trimmed = text.trim();
    if (!trimmed) return;
    const scopeChanged = (scope === "song") !== !!note.project;
    void updateNote(note.id, { text: trimmed, ...(scopeChanged ? { scope } : {}) }).then(() => setEditing(false), () => {});
  };

  if (editing) {
    return (
      <div className="rounded-[8px] bg-raised ring-1 ring-agent/50">
        <NoteField value={text} onChange={setText} onSubmit={save} onCancel={() => setEditing(false)} autoFocus />
        <div className="flex items-center gap-1 px-1.5 pb-1.5">
          {elsewhere ? <span className="ml-1.5 text-2xs text-faint">For {note.projectName}</span> : <ScopeSwitch value={scope} onChange={setScope} />}
          <div className="flex-1" />
          <button type="button" onClick={() => setEditing(false)} className="h-6 rounded-[4px] px-2 text-xs text-dim hover:text-fg">Cancel</button>
          <button type="button" disabled={!text.trim()} onClick={save}
            className="h-6 rounded-[4px] bg-agent px-2 text-xs text-black disabled:opacity-40">Save</button>
        </div>
      </div>
    );
  }

  return (
    <div className={cn("group rounded-[8px] bg-raised px-2.5 py-2 ring-1 ring-line", elsewhere && "opacity-70")}>
      <div onDoubleClick={startEditing}
        className={cn("text-xs leading-relaxed whitespace-pre-wrap break-words select-text", note.enabled ? "text-fg" : "text-faint line-through decoration-faint/50")}>
        {note.text}
      </div>
      <div className="mt-1.5 flex items-center gap-2">
        <Toggle on={note.enabled} onChange={(enabled) => void updateNote(note.id, { enabled }).catch(() => {})}
          title={note.enabled ? "The agent follows this note; click to set it aside" : "Set aside: click to have the agent follow it again"} />
        <span className="min-w-0 flex-1 truncate text-2xs text-faint">
          {!note.enabled ? "Off" : note.project ? (elsewhere ? `For ${note.projectName}` : "This song only") : "Every song"}
        </span>
        {confirming ? (
          <div className="flex shrink-0 items-center gap-1">
            <button type="button" className="rounded-[4px] bg-rec px-1.5 py-0.5 text-2xs text-black" onClick={() => {
              setConfirming(false);
              void deleteNote(note.id).catch(() => {});
            }}>Delete</button>
            <button type="button" className="text-faint hover:text-fg" title="Keep it" onClick={() => setConfirming(false)}><X size={11} /></button>
          </div>
        ) : (
          <div className="flex shrink-0 items-center gap-1.5 opacity-0 transition-opacity group-hover:opacity-100 focus-within:opacity-100">
            <button type="button" title="Edit" onClick={startEditing} className="text-faint hover:text-fg"><Pencil size={11} /></button>
            <button type="button" title="Delete this note" onClick={() => setConfirming(true)} className="text-faint hover:text-rec"><Trash2 size={11} /></button>
          </div>
        )}
      </div>
    </div>
  );
}

/** A note's text: grows with it; Enter submits, Shift+Enter starts a new line, Esc cancels. */
function NoteField({ value, onChange, onSubmit, onCancel, disabled, placeholder, autoFocus }: {
  value: string; onChange: (v: string) => void; onSubmit: () => void; onCancel?: () => void; disabled?: boolean; placeholder?: string; autoFocus?: boolean;
}) {
  const input = useRef<HTMLTextAreaElement>(null);

  useLayoutEffect(() => {
    const el = input.current;
    if (!el) return;
    el.style.height = "auto";
    el.style.height = `${Math.min(180, el.scrollHeight)}px`;
  }, [value]);

  useLayoutEffect(() => {
    const el = input.current;
    if (autoFocus && el) {
      el.focus();
      el.setSelectionRange(el.value.length, el.value.length);
    }
  }, [autoFocus]);

  return (
    <textarea
      ref={input}
      rows={1}
      value={value}
      disabled={disabled}
      placeholder={placeholder}
      onChange={(e) => onChange(e.target.value)}
      onKeyDown={(e) => {
        e.stopPropagation();
        if (e.key === "Enter" && !e.shiftKey && !e.nativeEvent.isComposing) {
          e.preventDefault();
          onSubmit();
        } else if (e.key === "Escape" && onCancel) {
          e.preventDefault();
          onCancel();
        }
      }}
      className="block max-h-[180px] min-h-[38px] w-full resize-none bg-transparent px-3 pt-2.5 pb-1 text-xs leading-5 text-fg outline-none placeholder:text-faint focus-visible:outline-none disabled:opacity-60"
    />
  );
}

function ScopeSwitch({ value, onChange }: { value: NoteScope; onChange: (v: NoteScope) => void }) {
  return (
    <div className="flex items-center rounded-full bg-control p-0.5">
      {([["all", "Every song"], ["song", "This song"]] as const).map(([v, label]) => (
        <button key={v} type="button" onClick={() => onChange(v)}
          className={cn("h-5 rounded-full px-2 text-2xs whitespace-nowrap transition-colors", value === v ? "bg-hover text-fg" : "text-dim hover:text-fg")}>
          {label}
        </button>
      ))}
    </div>
  );
}

function Toggle({ on, onChange, title }: { on: boolean; onChange: (on: boolean) => void; title: string }) {
  return (
    <button type="button" role="switch" aria-checked={on} title={title} onClick={() => onChange(!on)}
      className={cn("relative h-3.5 w-6 shrink-0 rounded-full transition-colors", on ? "bg-agent" : "bg-control ring-1 ring-line")}>
      <span className={cn("absolute top-0.5 h-2.5 w-2.5 rounded-full transition-[left,background-color]", on ? "left-[11px] bg-black" : "left-0.5 bg-dim")} />
    </button>
  );
}
