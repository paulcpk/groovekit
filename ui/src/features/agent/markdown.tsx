import type { Components } from "react-markdown";

// Replies are Markdown; images show their alt text only, so text that reached the model can't
// make the panel load a URL.
export const MARKDOWN: Components = {
  img: ({ alt }) => <>{alt}</>,
  p: ({ children }) => <p className="my-1.5 first:mt-0 last:mb-0">{children}</p>,
  ul: ({ children }) => <ul className="my-1.5 list-disc space-y-0.5 pl-4 marker:text-faint">{children}</ul>,
  ol: ({ children }) => <ol className="my-1.5 list-decimal space-y-0.5 pl-4 marker:text-faint">{children}</ol>,
  code: ({ children }) => <code className="rounded-[3px] bg-bg px-1 py-px font-mono text-[11px]">{children}</code>,
  pre: ({ children }) => <pre className="my-2 overflow-x-auto rounded-[6px] bg-bg p-2 text-[11px] ring-1 ring-line [&_code]:bg-transparent [&_code]:p-0">{children}</pre>,
  a: ({ children, href }) => <a href={href} target="_blank" rel="noreferrer" className="text-accent underline underline-offset-2">{children}</a>,
  h1: ({ children }) => <p className="mt-3 mb-1 font-semibold">{children}</p>,
  h2: ({ children }) => <p className="mt-3 mb-1 font-semibold">{children}</p>,
  h3: ({ children }) => <p className="mt-2 mb-1 font-semibold">{children}</p>,
  blockquote: ({ children }) => <blockquote className="my-1.5 border-l-2 border-line pl-2 text-dim">{children}</blockquote>,
  table: ({ children }) => <div className="my-2 overflow-x-auto"><table className="text-[11px]">{children}</table></div>,
  th: ({ children }) => <th className="border-b border-line px-1.5 py-0.5 text-left">{children}</th>,
  td: ({ children }) => <td className="border-b border-line/50 px-1.5 py-0.5">{children}</td>,
};
