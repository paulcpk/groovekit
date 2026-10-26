// Builds compact, always-current references for the agent from the schema package.

import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";

type Schema = Record<string, any>;

function readSchemaFile(name: string): string {
  return readFileSync(fileURLToPath(import.meta.resolve(`@groovekit/schema/${name}`)), "utf8");
}

function typeName(s: Schema): string {
  if (s.$ref) return s.$ref.split("/").pop();
  if (s.type === "array") return `${typeName(s.items)}[]`;
  if (s.enum) return s.enum.map((v: unknown) => JSON.stringify(v)).join(" | ");
  if (s.type === "integer") return "integer";
  return s.type ?? "any";
}

/** One line per command: `name { field: Type, optional?: Type } description`. */
export function commandReference(): string {
  const protocol = JSON.parse(readSchemaFile("protocol.schema.json"));
  const defs: Record<string, Schema> = protocol.$defs;
  const lines: string[] = [];

  for (const alternative of defs.Command.oneOf) {
    const def = defs[alternative.$ref.split("/").pop()];
    const fields = Object.entries<Schema>(def.properties)
      .filter(([name]) => name !== "type")
      .map(([name, s]) => `${name}${def.required?.includes(name) ? "" : "?"}: ${typeName(s)}`);

    lines.push(`- ${def.properties.type.const} { ${fields.join(", ")} }${def.description ? ` - ${def.description}` : ""}`);
  }

  return lines.join("\n");
}

/** The generated TypeScript definitions: the most compact complete description of the data model. */
export function typeDefinitions(which: "project" | "protocol"): string {
  return readFileSync(fileURLToPath(import.meta.resolve(`@groovekit/schema/${which}`)), "utf8");
}
