import type { PatchOp } from "@groovekit/schema/protocol";

export function parsePointer(path: string): string[] {
  return path.split("/").slice(1).map((k) => k.replace(/~1/g, "/").replace(/~0/g, "~"));
}

/** Applies JSON Patch add/remove/replace without mutating: every object on the way to a change is
    copied, everything else is shared, so React sees exactly what changed. */
export function applyPatch<T>(doc: T, ops: PatchOp[]): T {
  let result: unknown = doc;
  for (const op of ops) result = applyOne(result, parsePointer(op.path), 0, op);
  return result as T;
}

function applyOne(node: unknown, keys: string[], i: number, op: PatchOp): unknown {
  if (keys.length === 0) return op.op === "remove" ? undefined : op.value;

  const key = keys[i];
  const last = i === keys.length - 1;

  if (Array.isArray(node)) {
    const copy = node.slice();
    const index = key === "-" ? copy.length : Number(key);

    if (!last) copy[index] = applyOne(copy[index], keys, i + 1, op);
    else if (op.op === "add") copy.splice(index, 0, op.value);
    else if (op.op === "remove") copy.splice(index, 1);
    else copy[index] = op.value;

    return copy;
  }

  const copy: Record<string, unknown> = { ...(node as Record<string, unknown>) };

  if (!last) copy[key] = applyOne(copy[key], keys, i + 1, op);
  else if (op.op === "remove") delete copy[key];
  else copy[key] = op.value;

  return copy;
}
