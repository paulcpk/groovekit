import tailwindcss from "@tailwindcss/vite";
import react from "@vitejs/plugin-react";
import { resolve } from "node:path";
import { defineConfig } from "vite";
import { enginePlugin } from "./engine-plugin.ts";

export default defineConfig({
  plugins: [react(), tailwindcss(), enginePlugin()],
  resolve: { alias: { "@": resolve(import.meta.dirname, "src") } },
  server: { port: 5178, strictPort: true },
  clearScreen: false,
});
