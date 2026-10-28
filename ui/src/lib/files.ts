/** A folder picked with the system's dialog; desktop app only. */
export async function pickFolder(title: string, defaultPath?: string) {
  const { open } = await import("@tauri-apps/plugin-dialog");
  const picked = await open({ title, directory: true, defaultPath });
  return typeof picked === "string" ? picked : undefined;
}

/** A file picked with the system's dialog, of the given extensions; desktop app only. */
export async function pickFile(title: string, extensions: string[], defaultPath?: string) {
  const { open } = await import("@tauri-apps/plugin-dialog");
  const picked = await open({ title, defaultPath, filters: [{ name: extensions.join(", "), extensions }] });
  return typeof picked === "string" ? picked : undefined;
}

/** Where to save a file, from the system's dialog; desktop app only. */
export async function pickSavePath(title: string, defaultPath: string, extensions: string[]) {
  const { save } = await import("@tauri-apps/plugin-dialog");
  const picked = await save({ title, defaultPath, filters: [{ name: extensions.join(", "), extensions }] });
  return picked ?? undefined;
}
