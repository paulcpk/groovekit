import { KeyRound } from "lucide-react";
import { useState } from "react";

import { Button } from "@/components/controls";

import { setApiKey } from "./agent";

export function KeySetup() {
  const [key, setKey] = useState("");

  return (
    <div className="flex flex-col gap-3 p-3 text-xs">
      <div className="flex items-center gap-2 text-sm text-fg"><KeyRound size={14} className="text-agent" /> Connect Claude</div>
      <p className="leading-relaxed text-dim">
        The agent uses Claude with your own Anthropic API key. Create one in the Anthropic Console under API keys; use is billed to your
        Anthropic account. The key stays on this Mac. What you ask, and what the agent reads of the song to do it (names, notes, mix
        settings, sample file paths), is sent to Anthropic.
      </p>
      <form className="flex flex-col gap-2" onSubmit={(e) => {
        e.preventDefault();
        if (key.trim()) setApiKey(key);
      }}>
        <input type="password" autoComplete="off" value={key} onChange={(e) => setKey(e.target.value)} onKeyDown={(e) => e.stopPropagation()}
          placeholder="sk-ant-…" className="h-7 rounded-[6px] bg-bg px-2 text-xs text-fg outline-none ring-1 ring-line focus:ring-agent/60 focus-visible:outline-none" />
        <Button size="sm" type="submit" disabled={!key.trim()} className="self-start bg-agent text-black hover:bg-agent">Save key</Button>
      </form>
      <p className="text-faint">You can also use Claude Code: the agent tools are its MCP server (see the README).</p>
    </div>
  );
}
