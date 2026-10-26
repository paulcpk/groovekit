// The agent references, read from the schema package's files (Node).

import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { makeReference } from "./reference.ts";

const read = (name: string) => readFileSync(fileURLToPath(import.meta.resolve(`@groovekit/schema/${name}`)), "utf8");

export const fileReference = makeReference({
  protocolSchema: JSON.parse(read("protocol.schema.json")),
  projectTypes: read("project"),
  protocolTypes: read("protocol"),
});
