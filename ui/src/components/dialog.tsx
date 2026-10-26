import { Dialog as D } from "radix-ui";
import { X } from "lucide-react";
import { cn } from "@/lib/cn";

export function Dialog({ open, onOpenChange, title, children, className }: {
  open: boolean; onOpenChange: (open: boolean) => void; title: string; children: React.ReactNode; className?: string;
}) {
  return (
    <D.Root open={open} onOpenChange={onOpenChange}>
      <D.Portal>
        <D.Overlay className="fixed inset-0 z-40 bg-black/50" />
        <D.Content
          className={cn("fixed top-1/2 left-1/2 z-50 max-h-[85vh] w-[520px] max-w-[92vw] -translate-x-1/2 -translate-y-1/2 overflow-y-auto rounded-lg border border-line bg-panel p-4 shadow-2xl", className)}
          aria-describedby={undefined}
          // A field marked data-autofocus gets the focus rather than the close button.
          onOpenAutoFocus={(e) => {
            const field = (e.currentTarget as HTMLElement).querySelector<HTMLElement>("[data-autofocus]");
            if (!field) return;
            e.preventDefault();
            field.focus();
          }}
        >
          <div className="mb-3 flex items-center">
            <D.Title className="flex-1 text-sm font-semibold">{title}</D.Title>
            <D.Close className="rounded p-1 text-dim hover:bg-control hover:text-fg"><X size={14} /></D.Close>
          </div>
          {children}
        </D.Content>
      </D.Portal>
    </D.Root>
  );
}
