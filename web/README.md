# web

The GrooveKit website: one static page built with Astro and Tailwind CSS 4, with the Inter font
self-hosted. It ships no JavaScript; motion is CSS only and stops for visitors who prefer reduced
motion.

```sh
pnpm install                 # from the repository root (web is a workspace package)
pnpm --filter web dev        # http://localhost:4321 (WEB_PORT to change)
pnpm --filter web build      # static site in web/dist/
pnpm --filter web preview    # serve the build
pnpm --filter web typecheck  # astro sync + tsc
```

Set `SITE_URL` when building for a real domain (`SITE_URL=https://example.org pnpm --filter web build`)
to add canonical and Open Graph URLs.

## Where things live

| Path                     | What                                                                     |
| ------------------------ | ------------------------------------------------------------------------ |
| `src/lib/site.ts`        | Links, the agent tools and what they do, the FAQ                         |
| `src/components/`        | One component per section, in page order in `src/pages/index.astro`     |
| `src/components/AppWindow.astro` | The hero's picture of the app, drawn in HTML and CSS            |
| `src/styles/site.css`    | Colours (the app's palette: amber accent, violet for the agent), motion  |
| `public/`                | Favicon, touch icon, robots.txt                                          |

The tool list in `src/lib/site.ts` mirrors `mcp/src/tools.ts`; update both when a tool changes.
The pictures are drawn, not screenshots, so they don't go stale when the UI changes, but they should
still look like the app.

## Hosting

Every push to `main` that changes `web/` publishes the site on GitHub Pages at
https://groovekit.org (see `.github/workflows/pages.yml`; `public/CNAME` holds the domain). For
another host, build with `SITE_URL` (and `BASE_PATH` if it's served from a folder).
