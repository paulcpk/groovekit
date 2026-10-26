/** A folder picked with the system's dialog; desktop app only. */
export async function pickFolder(title: string, defaultPath?: string) {
  const { open } = await import("@tauri-apps/plugin-dialog");
  const picked = await open({ title, directory: true, defaultPath });
  return typeof picked === "string" ? picked : undefined;
}
