/**
 * Everything the page says that isn't layout: links, the agent's tools, the app's features and the
 * FAQ. Tool names and what they do come from mcp/src/tools.ts; keep them in step when tools change.
 */

export const NAME = "GrooveKit";
export const TAGLINE = "You direct. The agent produces.";
export const DESCRIPTION =
  "GrooveKit is an open-source, agent-first music production app for macOS. Claude arranges, mixes, automates and masters through the same API as the interface, and every change shows up live and undoes in one step.";

export const LINKS = {
  download: "https://github.com/paulcpk/groovekit/releases/latest/download/GrooveKit-macOS.zip",
  releases: "https://github.com/paulcpk/groovekit/releases",
  github: "https://github.com/paulcpk/groovekit",
  issues: "https://github.com/paulcpk/groovekit/issues",
  readme: "https://github.com/paulcpk/groovekit#readme",
  contributing: "https://github.com/paulcpk/groovekit/blob/main/CONTRIBUTING.md",
  license: "https://github.com/paulcpk/groovekit/blob/main/LICENSE",
  protocol: "https://github.com/paulcpk/groovekit/blob/main/docs/protocol.md",
  architecture: "https://github.com/paulcpk/groovekit/blob/main/docs/architecture.md",
  roadmap: "https://github.com/paulcpk/groovekit/blob/main/docs/roadmap.md",
  mcp: "https://modelcontextprotocol.io",
};

export const NAV: [id: string, label: string][] = [
  ["agent", "How it works"],
  ["tools", "Agent tools"],
  ["features", "Features"],
  ["hardware", "Hardware"],
  ["open-source", "Open source"],
  ["faq", "FAQ"],
];

/** The command the README gives for using GrooveKit from Claude Code in any folder. */
export const MCP_ADD = "claude mcp add --scope user groovekit -- node /path/to/groovekit/mcp/src/index.ts";

export interface ToolGroup {
  id: string;
  title: string;
  /** What the agent gets out of it, in one sentence. */
  lead: string;
  tools: { name: string; does: string }[];
  /** Wider card in the grid. */
  wide?: boolean;
}

export const TOOL_GROUPS: ToolGroup[
  ] = [
  {
    id: "sounds",
    title: "Picks sounds from your library",
    lead: "The agent searches the samples you already own, by words, category, loop, tempo and key, and reads a description of every sound before it uses it.",
    wide: true,
    tools: [
      {
        name: "search_samples",
        does: "Finds samples by words, category (kick, snare, hat, bass, synth, vocal, fx…), one-shot or loop, tempo (half and double time match) and key. Each result says what it sounds like: “kick one-shot, 0.42 s, pitched F1, sharp attack, dark, bass-heavy”, plus its detected root key.",
      },
      {
        name: "describe_sample",
        does: "Full measurements of one file: duration, loudness, attack and decay, spectral centroid, sub/low/mid/high energy, detected pitch, onsets and tempo. Works on files outside the library too.",
      },
      {
        name: "sample_library",
        does: "Adds or removes library folders and rescans changed files in the background.",
      },
    ],
  },
  {
    id: "ears",
    title: "Hears in numbers",
    lead: "The agent can’t listen, so it measures. It renders, reads the result, adjusts and renders again.",
    tools: [
      {
        name: "render",
        does: "Renders the song or a range of bars to a 48 kHz WAV and returns peak, true peak, RMS, integrated/short-term/momentary LUFS, loudness range, ten-band spectral balance, stereo correlation and onset times.",
      },
      { name: "analyze_audio", does: "The same measurements for any audio file, or a stretch of it." },
    ],
  },
  {
    id: "edit",
    title: "Edits atomically, undoes cleanly",
    lead: "One tool changes the song: a list of typed commands applied as a single, all-or-nothing step.",
    tools: [
      {
        name: "edit_project",
        does: "Adds and changes channels, sampler layers, patterns, notes, playlist tracks, clips, inserts, effects and sends. If any command fails, nothing changes and the error names the field.",
      },
      { name: "get_summary", does: "A compact overview: tempo, channels and samples, patterns, clips in bars, mixer." },
      { name: "get_project", does: "The exact song.json, or any part of it by JSON Pointer." },
      { name: "get_schema", does: "TypeScript definitions of the model and every command, with defaults." },
    ],
  },
  {
    id: "mix",
    title: "Finds what’s fighting in the mix",
    lead: "One stem per channel, measured against the whole.",
    tools: [
      {
        name: "analyze_mix",
        does: "Each source’s loudness and share of every frequency band, and masking: pairs that crowd the same band, like kick and bass at 60–120 Hz.",
      },
    ],
  },
  {
    id: "master",
    title: "Masters to a target",
    lead: "Streaming at −14 LUFS, a club at −9 to −7: name the number.",
    tools: [
      {
        name: "master",
        does: "Renders, measures and sets the master limiter’s input gain until the song is within 0.2 LU of the target, under a true-peak ceiling. Undoable like any edit.",
      },
    ],
  },
  {
    id: "plugins",
    title: "Drives your plugins",
    lead: "Your VST3 and AU instruments and effects, by name, with every parameter.",
    tools: [
      { name: "plugins", does: "Lists installed plugins by name, maker, category, instrument or effect; scans for new ones." },
      {
        name: "describe_plugin",
        does: "Loads a plugin and lists its parameters with their values, so the agent can set them by name and automate them with automation clips.",
      },
      { name: "list_effects", does: "The built-in eq, compressor, reverb, delay, phaser, chorus, lowpass and highpass, with every parameter’s unit and range." },
    ],
  },
  {
    id: "record",
    title: "Records your hardware",
    lead: "Per-track takes from a groovebox, landing on the grid.",
    tools: [
      {
        name: "record",
        does: "Sets the input device and sends MIDI clock so the hardware follows GrooveKit, calibrates the round trip once, then records every output into its own insert and playlist track. Silent takes are skipped; each take is levelled to about −20 LUFS.",
      },
    ],
  },
  {
    id: "transport",
    title: "Plays it for you",
    lead: "When it wants your ears, it presses play.",
    tools: [
      {
        name: "transport",
        does: "Plays the song or loops one pattern through your speakers, loops a range of bars, stops and moves the playhead.",
      },
    ],
  },
  {
    id: "history",
    title: "Every change, on the record",
    lead: "Who changed what, and when. One request is one undo step.",
    tools: [
      { name: "undo", does: "Reverts the last group of changes; edits made close together undo as one." },
      { name: "redo", does: "Brings them back." },
      { name: "get_history", does: "The full change history and the current position." },
    ],
  },
  {
    id: "project",
    title: "Works on your projects",
    lead: "The same project the app has open.",
    tools: [
      {
        name: "project",
        does: "Shows the open project and recent ones, opens another, creates a new one or saves a copy as. Your window follows.",
      },
    ],
  },
];

export const TOOL_COUNT = TOOL_GROUPS.reduce((n, g) => n + g.tools.length, 0);

/** Answers may mark `code` with backticks. */
export const FAQ: { q: string; a: string }[] = [
  {
    q: "What do I need to run it?",
    a: "A Mac with Apple silicon and macOS 12 Monterey or later. Download the zip, unzip it and move GrooveKit to Applications.",
  },
  {
    q: "macOS says it can’t open GrooveKit. What now?",
    a: "The app isn’t notarized by Apple yet, so the first time macOS says it couldn’t verify GrooveKit. Click Done (not Move to Trash), open System Settings → Privacy & Security, scroll down to “GrooveKit was blocked” and click Open Anyway, then confirm with your password. After that it opens normally. Or in Terminal: xattr -dr com.apple.quarantine /Applications/GrooveKit.app",
  },
  {
    q: "How do I connect Claude?",
    a: "GrooveKit’s MCP server lives in the repository. Clone it, run `pnpm install` (Node 24 or later), and register the server with Claude Code: `claude mcp add --scope user groovekit -- node /path/to/groovekit/mcp/src/index.ts`. Check `/mcp` in Claude Code, then ask for a beat. When the app has the same project open (by default `~/Music/GrooveKit/Scratch`; set `GROOVEKIT_PROJECT` for another), the agent works in your window and you watch every change happen.",
  },
  {
    q: "Do I need the app to use the agent?",
    a: "No, if you build the engine from source: when no app is serving the project, the MCP server starts its own engine, so Claude can build, render and master songs headless. Open the app on that project any time to see and play what it made.",
  },
  {
    q: "Is the agent going to wreck my song?",
    a: "Every agent edit is an atomic command in the project history. A request’s edits form one group, so a single undo takes the whole request back, and the Agent tab shows exactly what changed.",
  },
  {
    q: "Where do the sounds come from?",
    a: "From your own sample folders. GrooveKit indexes them in the background (tens of thousands of files are fine), describes each sound, and gives both you and the agent the same search. Your VST3 and AU plugins work too.",
  },
  {
    q: "Does it cost anything?",
    a: "No. GrooveKit is free and open source under the AGPLv3. You bring your own Claude access for the agent.",
  },
  {
    q: "Windows? Intel Macs?",
    a: "Not yet. The engine is cross-platform by design and a Windows build is on the roadmap, but today the app is built and tested for Apple silicon Macs.",
  },
  {
    q: "How finished is it?",
    a: "Early. The engine, command API, MCP server, sample library, analysis, mastering, live playback, recording, plugin hosting and a first desktop UI work. Expect rough edges, and please report them.",
  },
];
