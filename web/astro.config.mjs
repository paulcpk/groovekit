// The GrooveKit website: one static page, no JavaScript of its own. SITE_URL sets the canonical
// and social URLs (e.g. SITE_URL=https://groovekit.example pnpm build); without it they're left out.
// BASE_PATH serves it from a folder, as GitHub Pages does (BASE_PATH=/groovekit).
import tailwindcss from "@tailwindcss/vite";
import { defineConfig } from "astro/config";

export default defineConfig({
  site: process.env.SITE_URL || undefined,
  base: process.env.BASE_PATH || "/",
  server: { port: Number(process.env.WEB_PORT || 4321) },
  // Stylesheets as files, so a strict Content-Security-Policy (styles from this origin only) works.
  build: { inlineStylesheets: "never" },
  devToolbar: { enabled: false },
  vite: { plugins: [tailwindcss()] },
});
