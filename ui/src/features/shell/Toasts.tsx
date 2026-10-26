import { cn } from "@/lib/cn";
import { useToasts } from "@/state/engine";

export function Toasts() {
  const toasts = useToasts((s) => s.toasts);
  return (
    <div className="pointer-events-none fixed right-4 bottom-10 z-50 flex flex-col items-end gap-2">
      {toasts.map((t) => (
        <div
          key={t.id}
          className={cn(
            "pointer-events-auto max-w-sm rounded-md border px-3 py-2 text-xs shadow-xl shadow-black/40",
            t.tone === "error" ? "border-rec/40 bg-[#2a1717] text-[#ffb4b4]" : "border-line bg-raised text-fg",
          )}
        >
          {t.message}
        </div>
      ))}
    </div>
  );
}
