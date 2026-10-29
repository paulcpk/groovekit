import { Button } from "@/components/controls";
import { cn } from "@/lib/cn";
import { megabytes } from "./ListeningPanel";
import { downloadStarter, useStarter } from "./starter";

/** The one-time offer of the starter samples, and their progress coming in. Nothing once
    they're in the library. `brief` is the version for the empty library's message. */
export function StarterSamples({ brief = false }: { brief?: boolean }) {
  const status = useStarter((s) => s.status);
  if (!status || status.installed) return null;

  if (status.downloading) {
    return (
      <div className="flex w-full flex-col gap-1">
        <span>Downloading the starter samples… {Math.round(status.progress * 100)}% of {megabytes(status.totalBytes)}</span>
        <div className="h-1 overflow-hidden rounded-full bg-bg"><div className="h-full bg-accent transition-[width]" style={{ width: `${status.progress * 100}%` }} /></div>
      </div>
    );
  }

  return (
    <div className={cn("flex flex-col gap-1.5", brief && "items-center")}>
      {!brief && (
        <span>
          {status.files} free drum machine sounds, one-shots and loops to start with, all public domain:
          a one-time {megabytes(status.totalBytes)} download into {status.folder.replace(/^\/Users\/[^/]+/, "~")}.
        </span>
      )}
      {status.error && <span className="text-rec">{status.error}</span>}
      <Button size="sm" className={brief ? undefined : "self-start"} onClick={() => void downloadStarter()}>Download starter samples ({megabytes(status.totalBytes)})</Button>
    </div>
  );
}
