// Writes the license notices into the build as licenses.txt, which Settings → About shows:
// GrooveKit's own license, then every third-party component the app ships: the engine's C++
// libraries, the JavaScript packages in the UI bundle and the Rust crates of the desktop shell.
// A part whose sources aren't there (say, the engine isn't built) is left out with a warning.

import { execFileSync } from "node:child_process";
import { existsSync, readdirSync, readFileSync, statSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import type { Plugin } from "vite";

const repo = resolve(import.meta.dirname, "..");
const tracktion = join(repo, "engine/external/tracktion_engine/modules");
const juce = join(tracktion, "juce/modules");
const deps = ["release", "dev"].map((preset) => join(repo, "build", preset, "_deps")).find(existsSync) ?? "";
const spdx = join(deps, "nlohmann_json-src/LICENSES");

interface Component {
  name: string;
  version?: string;
  license: string;
  url?: string;
  /** Who holds the copyright, when the texts don't say. */
  copyright?: string;
  /** Files whose text is the license. */
  files: string[];
}

/** The engine's C++ libraries, and the third-party code inside JUCE and Tracktion that it compiles. */
function cppComponents(): Component[] {
  const list: Component[] = [
    { name: "JUCE", license: "AGPL-3.0-only", url: "https://juce.com", files: [join(tracktion, "juce/LICENSE.md")] },
    { name: "Tracktion Engine", license: "GPL-3.0-or-later", url: "https://github.com/Tracktion/tracktion_engine", files: [join(tracktion, "../LICENSE.md"), join(spdx, "GPL-3.0-only.txt")] },
    { name: "Rubber Band Library", license: "GPL-2.0-or-later", url: "https://breakfastquay.com/rubberband", files: [join(deps, "rubberband-src/COPYING")] },
    { name: "IXWebSocket", license: "BSD-3-Clause", url: "https://github.com/machinezone/IXWebSocket", files: [join(deps, "ixwebsocket-src/LICENSE.txt")] },
    { name: "whisper.cpp and ggml (and OpenAI's Whisper model)", license: "MIT", url: "https://github.com/ggml-org/whisper.cpp", files: [join(deps, "whisper-src/LICENSE")] },
    { name: "JSON for Modern C++", license: "MIT", url: "https://github.com/nlohmann/json", files: [join(deps, "nlohmann_json-src/LICENSE.MIT")] },

    // Inside JUCE
    { name: "FLAC (in JUCE)", license: "BSD-3-Clause", files: [join(juce, "juce_audio_formats/codecs/flac/Flac Licence.txt")] },
    { name: "Ogg Vorbis (in JUCE)", license: "BSD-3-Clause", files: [join(juce, "juce_audio_formats/codecs/oggvorbis/Ogg Vorbis Licence.txt")] },
    { name: "libpng (in JUCE)", license: "libpng-2.0", files: [join(juce, "juce_graphics/image_formats/pnglib/LICENSE")] },
    { name: "jpeglib (in JUCE)", license: "IJG", files: [join(juce, "juce_graphics/image_formats/jpglib/README")] },
    { name: "zlib (in JUCE)", license: "Zlib", files: [join(juce, "juce_core/zip/zlib/README")] },
    { name: "HarfBuzz (in JUCE)", license: "MIT-Modern-Variant", files: [join(juce, "juce_graphics/fonts/harfbuzz/COPYING")] },
    { name: "SheenBidi (in JUCE)", license: "Apache-2.0", files: [join(juce, "juce_graphics/unicode/sheenbidi/LICENSE")] },
    { name: "CHOC (in JUCE)", license: "ISC", files: [join(juce, "juce_javascript/choc/LICENSE.md")] },
    { name: "VST3 SDK (in JUCE)", license: "MIT", files: [join(juce, "juce_audio_processors/format_types/VST3_SDK/LICENSE.txt")] },

    // Inside Tracktion Engine
    { name: "SoundTouch (in Tracktion Engine)", license: "LGPL-2.1-or-later", url: "https://www.surina.net/soundtouch", copyright: "Copyright (c) Olli Parviainen", files: [] },
    { name: "Airwindows (in Tracktion Engine)", license: "MIT", url: "https://github.com/airwindows/airwindows", copyright: "Copyright (c) 2018 Chris Johnson", files: [join(spdx, "MIT.txt")] },
    { name: "CHOC (in Tracktion Engine)", license: "ISC", files: [join(tracktion, "3rd_party/choc/LICENSE.md")] },
    { name: "crill (in Tracktion Engine)", license: "BSL-1.0", files: [join(tracktion, "3rd_party/crill/LICENSE.txt")] },
    { name: "farbot (in Tracktion Engine)", license: "MIT", files: [join(tracktion, "tracktion_graph/3rd_party/farbot/LICENSE")] },
    { name: "libsamplerate (in Tracktion Engine)", license: "BSD-2-Clause", url: "https://github.com/libsndfile/libsamplerate", copyright: "Copyright (c) 2002-2016, Erik de Castro Lopo", files: [] },
    { name: "magic_enum (in Tracktion Engine)", license: "MIT", copyright: "Copyright (c) 2019 - 2023 Daniil Goncharov", url: "https://github.com/Neargye/magic_enum", files: [join(spdx, "MIT.txt")] },
    { name: "MPMCQueue (in Tracktion Engine)", license: "MIT", copyright: "Copyright (c) 2020 Erik Rigtorp", url: "https://github.com/rigtorp/MPMCQueue", files: [join(spdx, "MIT.txt")] },
    { name: "NanoRange (in Tracktion Engine)", license: "BSL-1.0", copyright: "Copyright (c) 2018 Tristan Brindle", url: "https://github.com/tcbrindle/NanoRange", files: [] },
    { name: "rpmalloc (in Tracktion Engine)", license: "Public domain", url: "https://github.com/mjansson/rpmalloc", files: [] },
    { name: "expected (in Tracktion Engine)", license: "CC0-1.0", url: "https://github.com/TartanLlama/expected", files: [] },
  ];

  const missing = list.flatMap((c) => c.files).filter((f) => !existsSync(f));
  if (missing.length) {
    console.warn(`licenses.txt: no C++ notices; build the engine first (missing ${missing[0]})`);
    return [];
  }
  return list;
}

/** A package's license files: LICENSE, COPYING, NOTICE and the like. */
function licenseFiles(dir: string) {
  try {
    return readdirSync(dir)
      .filter((f) => /^(licen[cs]e|copying|notice)/i.test(f) && statSync(join(dir, f)).isFile())
      .map((f) => join(dir, f));
  } catch {
    return [];
  }
}

/** The npm packages bundled into the UI, from the module ids of the build. */
function jsComponents(ids: Iterable<string>): Component[] {
  const roots = new Set<string>();
  for (const id of ids) {
    const path = id.replace(/^\0/, "").split("?")[0];
    const at = path.lastIndexOf("/node_modules/");
    if (at < 0) continue;
    const rest = path.slice(at + "/node_modules/".length).split("/");
    roots.add(join(path.slice(0, at), "node_modules", ...rest.slice(0, rest[0].startsWith("@") ? 2 : 1)));
  }

  return [...roots].flatMap((dir) => {
    try {
      const pkg = JSON.parse(readFileSync(join(dir, "package.json"), "utf8"));
      const repository = typeof pkg.repository === "string" ? pkg.repository : pkg.repository?.url;
      return [{ name: pkg.name, version: pkg.version, license: pkg.license ?? "see below", url: pkg.homepage ?? repository, files: licenseFiles(dir) }];
    } catch {
      return [];
    }
  });
}

interface CargoMetadata {
  packages: { id: string; name: string; version: string; license?: string; repository?: string; manifest_path: string }[];
  resolve: { root: string; nodes: { id: string; deps: { pkg: string; dep_kinds: { kind: string | null }[] }[] }[] };
}

/** The Rust crates the desktop shell is built from: the shell's normal dependencies, all the way down. */
function rustComponents(): Component[] {
  let metadata: CargoMetadata;
  try {
    const host = execFileSync("rustc", ["-vV"]).toString().match(/host: (\S+)/)![1];
    const out = execFileSync("cargo", ["metadata", "--format-version", "1", "--locked", "--filter-platform", host, "--manifest-path", join(repo, "ui/src-tauri/Cargo.toml")], { maxBuffer: 1 << 28 });
    metadata = JSON.parse(out.toString());
  } catch (err) {
    console.warn(`licenses.txt: no Rust notices (${(err as Error).message.split("\n")[0]})`);
    return [];
  }

  const nodes = new Map(metadata.resolve.nodes.map((n) => [n.id, n]));
  const used = new Set<string>();
  const visit = (id: string) => {
    if (used.has(id)) return;
    used.add(id);
    for (const dep of nodes.get(id)?.deps ?? []) if (dep.dep_kinds.some((k) => k.kind === null)) visit(dep.pkg);
  };
  visit(metadata.resolve.root);
  used.delete(metadata.resolve.root);

  return metadata.packages
    .filter((p) => used.has(p.id))
    .sort((a, b) => a.name.localeCompare(b.name))
    .map((p) => ({ name: p.name, version: p.version, license: p.license ?? "see below", url: p.repository, files: licenseFiles(dirname(p.manifest_path)) }));
}

const RULE = "-".repeat(80);

function section(title: string, components: Component[], seen: Map<string, string>) {
  if (!components.length) return "";
  const parts = components.map((c) => {
    const label = c.version ? `${c.name} ${c.version}` : c.name;
    const lines = [RULE, label, `License: ${c.license}`];
    if (c.url) lines.push(c.url.replace(/^git\+/, ""));
    if (c.copyright) lines.push(c.copyright);
    for (const file of c.files) {
      const text = readFileSync(file, "utf8").trim();
      // The same text (an Apache license, say) is written out once.
      const key = text.replace(/\s+/g, " ");
      const first = seen.get(key);
      if (first) lines.push("", `(The same text as for ${first}.)`);
      else {
        seen.set(key, label);
        lines.push("", text);
      }
    }
    return lines.join("\n");
  });
  return `\n\n${title}\n${"=".repeat(title.length)}\n\n${parts.join("\n\n")}`;
}

export function noticesPlugin(): Plugin {
  return {
    name: "groovekit-notices",
    apply: "build",
    generateBundle() {
      const seen = new Map<string, string>();
      const header = [
        "GrooveKit",
        "Copyright (C) 2026 Paul Csapak and the GrooveKit contributors",
        "",
        "GrooveKit is free software: you can redistribute it and/or modify it under the terms of the",
        "GNU Affero General Public License as published by the Free Software Foundation, version 3.",
        "It comes WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or",
        "FITNESS FOR A PARTICULAR PURPOSE. The source code is at https://github.com/paulcpk/groovekit",
        "",
        "It includes the third-party software listed after its license, under their own licenses.",
        "",
        readFileSync(join(repo, "LICENSE"), "utf8").trim(),
      ].join("\n");
      seen.set(readFileSync(join(repo, "LICENSE"), "utf8").trim().replace(/\s+/g, " "), "GrooveKit");

      const source =
        header +
        section("The audio engine (C++)", cppComponents(), seen) +
        section("The interface (JavaScript)", jsComponents(this.getModuleIds()), seen) +
        section("The desktop app (Rust)", rustComponents(), seen) +
        "\n";
      this.emitFile({ type: "asset", fileName: "licenses.txt", source });
    },
  };
}
