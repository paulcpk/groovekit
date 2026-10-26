import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import { App } from "./App";
import { engine } from "./state/engine";
import "./index.css";

engine.start();

createRoot(document.getElementById("root")!).render(
  <StrictMode>
    <App />
  </StrictMode>,
);
