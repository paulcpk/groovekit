import tailwindcss from "@tailwindcss/vite";
import react from "@vitejs/plugin-react";
import { execFileSync } from "node:child_process";
import { resolve } from "node:path";
import { defineConfig } from "vite";
import { enginePlugin } from "./engine-plugin.ts";
import { noticesPlugin } from "./notices-plugin.ts";

/** Which build this is: its release tag, or how far past one (e.g. 0.3.0-14-ge327b99); empty
    without git history (release builds then take the tag's version from Tauri). */
function buildVersion() {
  try {
    return execFileSync("git", ["describe", "--tags", "--dirty"], { cwd: import.meta.dirname, stdio: ["ignore", "pipe", "ignore"] })
      .toString().trim().replace(/^v/, "");
  } catch {
    return "";
  }
}

export default defineConfig({
  define: { "import.meta.env.GROOVEKIT_VERSION": JSON.stringify(buildVersion()) },
  plugins: [react(), tailwindcss(), enginePlugin(), noticesPlugin()],
  resolve: { alias: { "@": resolve(import.meta.dirname, "src") } },
  server: { port: 5178, strictPort: true },
  clearScreen: false,
});
