# pl_editor

- Upstream: <https://github.com/plos-clan/pl_editor>
- Revision: `15435b8aab125537421d57c6220e3b8a0c827787`
- License: MIT, see `LICENSE`.

This directory contains the editor core, syntax highlighting and platform
interface. Plant OS provides the entry point and platform implementation in
`apps/editor/`; the Linux/Windows entry points and xmake build are not used.

Local changes use a checked screen-output builder, bound status/gutter rendering,
preserve raw file bytes when finding line boundaries, and distinguish file-read
errors from new files. Allocations go through the platform's checked allocator;
document, row and output growth are checked before allocation. Newline redo
reuses insertion, and empty-document navigation/search and syntax offsets are
handled in the core. A failed Save As keeps the document unnamed for retry.
No terminal parser is embedded in the editor.
