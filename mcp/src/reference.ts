// Builds compact, always-current references for the agent from the schema package. The sources
// are passed in, so this runs both in Node (reference-files.ts reads them) and in the app's web
// view (which bundles them).

type Schema = Record<string, any>;

/** The schema package's files the references are made from. */
export interface ReferenceSources {
  protocolSchema: Schema;     // protocol.schema.json
  projectTypes: string;       // generated/project.ts
  protocolTypes: string;      // generated/protocol.ts
}

export interface Reference {
  /** One line per command: `name { field: Type, optional?: Type } description`. */
  commandReference(): string;
  /** The generated TypeScript definitions: the most compact complete description of the data model. */
  typeDefinitions(which: "project" | "protocol"): string;
}

function typeName(s: Schema): string {
  if (s.$ref) return s.$ref.split("/").pop();
  if (s.type === "array") return `${typeName(s.items)}[]`;
  if (s.enum) return s.enum.map((v: unknown) => JSON.stringify(v)).join(" | ");
  if (s.type === "integer") return "integer";
  return s.type ?? "any";
}

export function makeReference(sources: ReferenceSources): Reference {
  return {
    commandReference() {
      const defs: Record<string, Schema> = sources.protocolSchema.$defs;
      const lines: string[] = [];

      for (const alternative of defs.Command.oneOf) {
        const def = defs[alternative.$ref.split("/").pop()];
        const fields = Object.entries<Schema>(def.properties)
          .filter(([name]) => name !== "type")
          .map(([name, s]) => `${name}${def.required?.includes(name) ? "" : "?"}: ${typeName(s)}`);

        lines.push(`- ${def.properties.type.const} { ${fields.join(", ")} }${def.description ? ` - ${def.description}` : ""}`);
      }

      return lines.join("\n");
    },

    typeDefinitions(which) {
      return which === "project" ? sources.projectTypes : sources.protocolTypes;
    },
  };
}
