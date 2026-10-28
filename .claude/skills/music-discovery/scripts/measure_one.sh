#!/bin/sh
# Measures one audio file with GrooveKit's analyzer (loudness, crest, spectrum) into <out-folder>,
# keyed by the path's hash; the path is saved beside it. Usage: measure_one.sh <file> <out-folder>
f="$1"; outdir="$2"
engine="$(cd "$(dirname "$0")/../../../.." && pwd)/build/dev/engine/groovekit-engine"
mkdir -p "$outdir"
key=$(printf "%s" "$f" | md5 -q)
out="$outdir/$key.json"
[ -s "$out" ] && exit 0
"$engine" analyze "$f" 2>/dev/null | sed -n '/^{/,$p' > "$out.tmp" && printf "%s\n" "$f" > "$out.path" && mv "$out.tmp" "$out"
