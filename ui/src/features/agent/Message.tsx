import { Check, Copy } from "lucide-react";
import { useRef, useState } from "react";

import { ContextMenu, ContextMenuContent, ContextMenuItem, ContextMenuTrigger } from "@/components/menu";

import { cn } from "@/lib/cn";

export const copy = (text: string) => navigator.clipboard?.writeText(text);

/** Text in the conversation that can be selected, with the menu a right click is expected to
    bring up: copy what's selected, copy the whole message, select it. */
export function Message({ text, className, children }: { text: string; className?: string; children: React.ReactNode }) {
  const box = useRef<HTMLDivElement>(null);
  const [selected, setSelected] = useState("");

  // What's selected inside this message when the menu opens.
  const selectionHere = () => {
    const selection = window.getSelection();
    const el = box.current;
    if (!selection || selection.isCollapsed || !el) return "";
    return el.contains(selection.anchorNode) || el.contains(selection.focusNode) ? selection.toString() : "";
  };

  return (
    <ContextMenu onOpenChange={(open) => open && setSelected(selectionHere())}>
      <ContextMenuTrigger asChild>
        <div ref={box} className={cn("cursor-text select-text", className)}>{children}</div>
      </ContextMenuTrigger>
      <ContextMenuContent>
        <ContextMenuItem disabled={!selected} shortcut="⌘C" onSelect={() => void copy(selected)}>Copy</ContextMenuItem>
        <ContextMenuItem onSelect={() => void copy(text)}>Copy message</ContextMenuItem>
        <ContextMenuItem onSelect={() => {
          const el = box.current;
          const selection = window.getSelection();
          if (!el || !selection) return;
          const range = document.createRange();
          range.selectNodeContents(el);
          selection.removeAllRanges();
          selection.addRange(range);
        }}>Select message</ContextMenuItem>
      </ContextMenuContent>
    </ContextMenu>
  );
}

export function CopyButton({ text }: { text: string }) {
  const [copied, setCopied] = useState(false);
  return (
    <button type="button" title={copied ? "Copied" : "Copy"}
      className="mt-1 flex h-5 w-5 shrink-0 items-center justify-center rounded-[4px] text-faint opacity-0 transition-opacity group-hover:opacity-100 hover:bg-hover hover:text-fg focus-visible:opacity-100"
      onClick={() => void copy(text)?.then(() => {
        setCopied(true);
        setTimeout(() => setCopied(false), 1500);
      })}>
      {copied ? <Check size={11} /> : <Copy size={11} />}
    </button>
  );
}
