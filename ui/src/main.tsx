import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import { App } from "./App";
import { engine, useEngine } from "./state/engine";
import { useUi } from "./state/ui";
import "./index.css";

engine.start();

// In development the stores can be reached from the console, to measure and script the UI.
if (import.meta.env.DEV) Object.assign(window, { groovekit: { useEngine, useUi, engine } });

createRoot(document.getElementById("root")!).render(
  <StrictMode>
    <App />
  </StrictMode>,
);
