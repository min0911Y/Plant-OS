#!/usr/bin/env python3
import argparse
import bisect
import html
import re
import subprocess
import sys
import zlib
from collections import Counter
from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path


RECORD_RE = re.compile(
    r"^\s*([KU])\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)(?:\s+(.*))?\s*$"
)
SYMBOL_RE = re.compile(r"^([0-9a-fA-F]+)\s+([A-Za-z])\s+(.+)$")
UINT32_MAX = 0xFFFFFFFF
ADDRESS_MAX = 0xFFFFFFFFFFFFFFFF
PERF_STACK_MAX_DEPTH = 16


class PerfFormatError(ValueError):
    pass


class SampleKind(Enum):
    KERNEL = "K"
    USER = "U"


@dataclass(frozen=True)
class StackRecord:
    kind: SampleKind
    count: int
    cpu: int
    tid: int
    generation: int
    pcs: tuple[int, ...]


@dataclass
class PerfDump:
    metadata: dict[str, str]
    records: list[StackRecord] = field(default_factory=list)

    @property
    def reason(self):
        return self.metadata.get("reason", "unspecified")

    @property
    def session(self):
        return self.metadata["session"]

    @property
    def samples(self):
        return int(self.metadata["samples"])

    @property
    def dropped(self):
        return int(self.metadata["dropped"])

    @property
    def text_bounds(self):
        return (
            int(self.metadata["text_start"], 16),
            int(self.metadata["text_end"], 16),
        )

    def validate(self):
        if self.metadata.get("version") != "2":
            raise PerfFormatError("unsupported or missing PERF dump version")
        if self.metadata.get("session") not in {"boot", "manual"}:
            raise PerfFormatError("invalid or missing PERF session type")
        for key in ("samples", "stacks", "dropped"):
            try:
                value = int(self.metadata[key])
            except (KeyError, ValueError) as exc:
                raise PerfFormatError(f"invalid PERF header field: {key}") from exc
            if value < 0 or value > UINT32_MAX:
                raise PerfFormatError(f"out-of-range PERF header field: {key}")
        try:
            text_start, text_end = self.text_bounds
        except (KeyError, ValueError) as exc:
            raise PerfFormatError("invalid or missing PERF text bounds") from exc
        if text_start < 0 or text_start >= text_end or text_end > ADDRESS_MAX:
            raise PerfFormatError("out-of-range PERF text bounds")

        expected_stacks = int(self.metadata["stacks"])
        if expected_stacks != len(self.records):
            raise PerfFormatError(
                f"PERF header declares {expected_stacks} stacks, "
                f"but {len(self.records)} were parsed"
            )
        recorded = sum(record.count for record in self.records)
        if recorded != self.samples:
            raise PerfFormatError(
                f"PERF header declares {self.samples} samples, "
                f"but stack counts total {recorded}"
            )


@dataclass
class SymbolTable:
    addresses: list[int]
    names: list[str]
    text_start: int
    text_end: int

    @classmethod
    def load(cls, kernel, nm):
        try:
            process = subprocess.run(
                [nm, "-n", "--defined-only", str(kernel)],
                check=True,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
        except (OSError, subprocess.CalledProcessError) as exc:
            raise PerfFormatError(f"failed to read symbols from {kernel}: {exc}") from exc

        symbols = []
        boundaries = {}
        for line in process.stdout.splitlines():
            match = SYMBOL_RE.match(line)
            if match is None:
                continue
            address = int(match.group(1), 16)
            symbol_type = match.group(2).lower()
            name = match.group(3).split()[0]
            if name in {"__kernel_text_start", "__kernel_text_end"}:
                boundaries[name] = address
            elif symbol_type in {"t", "w"}:
                symbols.append((address, name))

        if not symbols:
            raise PerfFormatError(f"no text symbols found in {kernel}")
        if "__kernel_text_start" not in boundaries or "__kernel_text_end" not in boundaries:
            raise PerfFormatError(
                f"{kernel} does not expose the profiler text boundaries; rebuild it"
            )

        symbols.sort(key=lambda symbol: symbol[0])
        return cls(
            [symbol[0] for symbol in symbols],
            [symbol[1] for symbol in symbols],
            boundaries["__kernel_text_start"],
            boundaries["__kernel_text_end"],
        )

    def resolve(self, address, show_offset=False):
        if address < self.text_start or address >= self.text_end:
            return f"0x{address:x}"
        index = bisect.bisect_right(self.addresses, address) - 1
        if index < 0:
            return f"0x{address:x}"

        name = self.names[index]
        offset = address - self.addresses[index]
        return f"{name}+0x{offset:x}" if show_offset and offset else name


@dataclass
class FlameNode:
    name: str
    count: int = 0
    children: dict[str, "FlameNode"] = field(default_factory=dict)

    def add(self, frames, count):
        self.count += count
        node = self
        for frame_name in frames:
            child = node.children.get(frame_name)
            if child is None:
                child = FlameNode(frame_name)
                node.children[frame_name] = child
            child.count += count
            node = child

    def depth(self):
        if not self.children:
            return 0
        return 1 + max(child.depth() for child in self.children.values())


def parse_metadata(text):
    metadata = {}
    for token in text.split():
        if "=" in token:
            key, value = token.split("=", 1)
            metadata[key] = value
    return metadata


def parse_record(line, line_number):
    match = RECORD_RE.match(line)
    if match is None:
        raise PerfFormatError(f"malformed PERF record on line {line_number}")

    kind = SampleKind(match.group(1))
    count, cpu, tid, generation, depth = (
        int(match.group(index)) for index in range(2, 7)
    )
    if count == 0:
        raise PerfFormatError(f"zero-count PERF record on line {line_number}")
    if max(count, tid, generation) > UINT32_MAX or cpu > 0xFF:
        raise PerfFormatError(f"out-of-range metadata on line {line_number}")
    if depth > PERF_STACK_MAX_DEPTH:
        raise PerfFormatError(f"out-of-range stack depth on line {line_number}")

    tokens = (match.group(7) or "").split()
    if len(tokens) != depth:
        raise PerfFormatError(
            f"PERF record on line {line_number} declares depth {depth}, "
            f"but contains {len(tokens)} addresses"
        )
    try:
        pcs = tuple(int(token, 16) for token in tokens)
    except ValueError as exc:
        raise PerfFormatError(f"invalid address on line {line_number}") from exc
    if any(pc < 0 or pc > ADDRESS_MAX for pc in pcs):
        raise PerfFormatError(f"out-of-range address on line {line_number}")
    if kind is SampleKind.KERNEL and not pcs:
        raise PerfFormatError(f"empty kernel stack on line {line_number}")
    if kind is SampleKind.USER and pcs:
        raise PerfFormatError(f"user record has kernel frames on line {line_number}")
    return StackRecord(kind, count, cpu, tid, generation, pcs)


def read_perf_dumps(stream):
    dumps = []
    current = None

    for line_number, line in enumerate(stream, 1):
        if "PERF_BEGIN" in line:
            if current is not None:
                raise PerfFormatError(
                    f"nested PERF_BEGIN before line {line_number}"
                )
            current = PerfDump(parse_metadata(line.split("PERF_BEGIN", 1)[1]))
            continue
        if "PERF_END" in line:
            if current is None:
                continue
            current.validate()
            dumps.append(current)
            current = None
            continue
        if current is None:
            continue

        stripped = line.lstrip()
        if stripped.startswith(("K ", "U ")):
            current.records.append(parse_record(line, line_number))

    if current is not None:
        raise PerfFormatError("serial log ends inside an incomplete PERF dump")
    if not dumps:
        raise PerfFormatError("no complete PERF dump found in the serial log")
    return dumps


def task_label(record):
    if record.tid == UINT32_MAX:
        return "tid=unknown"
    return f"tid={record.tid}/{record.generation}"


def fold_dump(dump, symbols, group_by, show_offsets, include_user):
    folded = Counter()
    for record in dump.records:
        if record.kind is SampleKind.USER:
            if not include_user:
                continue
            frames = ["[user]"]
        else:
            frames = [symbols.resolve(pc, show_offsets) for pc in reversed(record.pcs)]

        if group_by in {"cpu", "cpu-task"}:
            frames.insert(0, f"cpu={record.cpu}")
        if group_by in {"task", "cpu-task"}:
            frames.insert(1 if group_by == "cpu-task" else 0, task_label(record))
        folded[";".join(frames)] += record.count
    return folded


def write_folded(path, folded):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as output:
        for stack, count in sorted(folded.items()):
            output.write(f"{stack} {count}\n")


def flame_color(name):
    value = zlib.crc32(name.encode("utf-8"))
    return f"rgb({220 + (value & 31)},{70 + ((value >> 5) & 95)},{30 + ((value >> 12) & 31)})"


def write_svg(path, folded, title, subtitle):
    root = FlameNode("")
    for stack, count in folded.items():
        root.add(stack.split(";"), count)
    if root.count == 0:
        raise PerfFormatError("the selected dump contains no requested samples")

    canvas_width = 1200
    margin = 10
    plot_width = canvas_width - margin * 2
    frame_height = 18
    plot_top = 62
    max_depth = root.depth()
    canvas_height = plot_top + max_depth * frame_height + 28
    elements = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{canvas_width}" '
        f'height="{canvas_height}" viewBox="0 0 {canvas_width} {canvas_height}" '
        f'style="width:100%;height:auto" data-margin="{margin}" '
        f'data-plot-width="{plot_width}" data-plot-top="{plot_top}" '
        f'data-frame-height="{frame_height}" data-max-depth="{max_depth}" '
        f'data-samples="{root.count}">',
        "<style>text{font-family:Verdana,sans-serif}"
        ".frame{cursor:pointer}.frame:hover{opacity:.75}"
        "#reset{cursor:pointer;text-decoration:underline}</style>",
        '<rect width="100%" height="100%" fill="#fff"/>',
        f'<text x="{canvas_width / 2:g}" y="24" text-anchor="middle" '
        f'font-size="17">{html.escape(title)}</text>',
        f'<text x="{canvas_width / 2:g}" y="44" text-anchor="middle" '
        f'font-size="12" fill="#555">{html.escape(subtitle)}</text>',
        f'<text id="reset" x="{canvas_width - margin}" y="24" '
        'text-anchor="end" font-size="12" fill="#075985" '
        'visibility="hidden" role="button" tabindex="0">Reset Zoom</text>',
    ]

    scale = plot_width / root.count

    def render(node, x, level, path_prefix):
        child_x = x
        children = sorted(node.children.values(), key=lambda item: item.name)
        for index, child in enumerate(children):
            width = child.count * scale
            if width >= 0.2:
                y = plot_top + (max_depth - level - 1) * frame_height
                percentage = child.count * 100 / root.count
                tooltip = html.escape(
                    f"{child.name} — {child.count} samples ({percentage:.2f}%)"
                )
                escaped_name = html.escape(child.name, quote=True)
                node_path = f"{path_prefix}/{index}" if path_prefix else str(index)
                elements.append(
                    f'<g class="frame" data-name="{escaped_name}" '
                    f'data-count="{child.count}" data-path="{node_path}" '
                    f'data-x="{child_x:.6f}" data-width="{width:.6f}" '
                    f'data-level="{level}" role="button" tabindex="0" '
                    f'aria-label="{tooltip}">'
                )
                elements.append(f"<title>{tooltip}</title>")
                elements.append(
                    f'<rect x="{child_x:.3f}" y="{y}" width="{max(width - 0.4, 0.1):.3f}" '
                    f'height="{frame_height - 1}" rx="2" fill="{flame_color(child.name)}"/>'
                )
                available = max(int((width - 6) / 7), 0)
                label = ""
                if available >= 3:
                    label = child.name
                    if len(label) > available:
                        label = label[: available - 2] + ".."
                elements.append(
                    f'<text x="{child_x + 3:.3f}" y="{y + 13}" '
                    f'font-size="12">{html.escape(label)}</text>'
                )
                elements.append("</g>")
                render(child, child_x, level + 1, node_path)
            child_x += width

    render(root, margin, 0, "")
    elements.append(
        f'<text id="details" x="{margin}" y="{canvas_height - 8}" '
        'font-size="11" fill="#555">'
        "Click a frame to zoom; hover to show its complete name.</text>"
    )
    elements.append(
        """<script><![CDATA[
(() => {
  const svg = document.documentElement;
  const frames = Array.from(svg.querySelectorAll(".frame"));
  const reset = svg.querySelector("#reset");
  const details = svg.querySelector("#details");
  const margin = Number(svg.dataset.margin);
  const plotWidth = Number(svg.dataset.plotWidth);
  const plotTop = Number(svg.dataset.plotTop);
  const frameHeight = Number(svg.dataset.frameHeight);
  const maxDepth = Number(svg.dataset.maxDepth);
  const total = Number(svg.dataset.samples);
  const hint = "Click a frame to zoom; hover to show its complete name.";

  const updateLabel = (frame, width) => {
    const text = frame.querySelector("text");
    const name = frame.dataset.name;
    const room = width - 6;
    text.textContent = name;
    if (room > 0 && text.getComputedTextLength() <= room) {
      return;
    }

    let low = 0;
    let high = name.length;
    while (low < high) {
      const middle = Math.ceil((low + high) / 2);
      text.textContent = `${name.slice(0, middle)}..`;
      if (text.getComputedTextLength() <= room) {
        low = middle;
      } else {
        high = middle - 1;
      }
    }
    text.textContent = low === 0 ? "" : `${name.slice(0, low)}..`;
  };

  const setGeometry = (frame, x, width, level) => {
    const y = plotTop + (maxDepth - level - 1) * frameHeight;
    const rect = frame.querySelector("rect");
    const text = frame.querySelector("text");
    rect.setAttribute("x", x.toFixed(3));
    rect.setAttribute("y", y);
    rect.setAttribute("width", Math.max(width - 0.4, 0.1).toFixed(3));
    text.setAttribute("x", (x + 3).toFixed(3));
    text.setAttribute("y", y + 13);
    updateLabel(frame, width);
  };

  const showDetails = (frame) => {
    const count = Number(frame.dataset.count);
    const percent = (count * 100 / total).toFixed(2);
    details.textContent = `${frame.dataset.name} — ${count} samples (${percent}%)`;
  };

  const restore = () => {
    for (const frame of frames) {
      frame.style.removeProperty("display");
      setGeometry(
        frame,
        Number(frame.dataset.x),
        Number(frame.dataset.width),
        Number(frame.dataset.level),
      );
    }
    reset.setAttribute("visibility", "hidden");
    details.textContent = hint;
  };

  const zoom = (target) => {
    const targetPath = target.dataset.path;
    const targetX = Number(target.dataset.x);
    const targetWidth = Number(target.dataset.width);
    const targetLevel = Number(target.dataset.level);
    const descendantPrefix = `${targetPath}/`;

    for (const frame of frames) {
      const path = frame.dataset.path;
      const visible = path === targetPath || path.startsWith(descendantPrefix);
      frame.style.display = visible ? "" : "none";
      if (!visible) {
        continue;
      }
      const x = margin +
        (Number(frame.dataset.x) - targetX) * plotWidth / targetWidth;
      const width = Number(frame.dataset.width) * plotWidth / targetWidth;
      const level = Number(frame.dataset.level) - targetLevel;
      setGeometry(frame, x, width, level);
    }
    reset.setAttribute("visibility", "visible");
    showDetails(target);
  };

  for (const frame of frames) {
    frame.addEventListener("click", (event) => {
      event.stopPropagation();
      zoom(frame);
    });
    frame.addEventListener("mouseenter", () => showDetails(frame));
    frame.addEventListener("mouseleave", () => { details.textContent = hint; });
    frame.addEventListener("keydown", (event) => {
      if (event.key === "Enter" || event.key === " ") {
        event.preventDefault();
        zoom(frame);
      }
    });
  }
  reset.addEventListener("click", restore);
  reset.addEventListener("keydown", (event) => {
    if (event.key === "Enter" || event.key === " ") {
      event.preventDefault();
      restore();
    }
  });
  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape") {
      restore();
    }
  });
  requestAnimationFrame(restore);
})();
]]></script>"""
    )
    elements.append("</svg>")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(elements) + "\n", encoding="utf-8")


def load_dumps(path):
    if path == "-":
        return read_perf_dumps(sys.stdin)
    with Path(path).open("r", encoding="utf-8", errors="replace") as stream:
        return read_perf_dumps(stream)


def main():
    parser = argparse.ArgumentParser(
        description="Convert a Plant OS serial CPU profile to folded stacks and SVG."
    )
    parser.add_argument(
        "--kernel", required=True, type=Path, help="kernel ELF with symbols"
    )
    parser.add_argument(
        "--serial", required=True, help="serial log, or - to read standard input"
    )
    parser.add_argument(
        "--out", required=True, type=Path, help="folded stack output path"
    )
    parser.add_argument("--svg", type=Path, help="SVG output path")
    parser.add_argument("--no-svg", action="store_true", help="only write folded stacks")
    parser.add_argument(
        "--dump",
        type=int,
        default=-1,
        help="zero-based dump index; negative indexes count from the end (default: -1)",
    )
    parser.add_argument(
        "--group-by",
        choices=("none", "cpu", "task", "cpu-task"),
        default="none",
        help="prepend CPU and/or task identity to each stack",
    )
    parser.add_argument(
        "--exclude-user",
        action="store_true",
        help="omit the aggregate [user] CPU-time frame",
    )
    parser.add_argument(
        "--show-offsets", action="store_true", help="retain instruction offsets"
    )
    parser.add_argument("--nm", default="nm", help="nm-compatible symbol reader")
    parser.add_argument("--title", default="Plant OS kernel CPU profile")
    args = parser.parse_args()

    try:
        dumps = load_dumps(args.serial)
        dump_index = args.dump if args.dump >= 0 else len(dumps) + args.dump
        if dump_index < 0 or dump_index >= len(dumps):
            raise PerfFormatError(
                f"dump index {args.dump} is out of range for {len(dumps)} dumps"
            )
        selected = dumps[dump_index]
        symbols = SymbolTable.load(args.kernel, args.nm)
        if selected.text_bounds != (symbols.text_start, symbols.text_end):
            raise PerfFormatError(
                "the selected dump does not match the supplied kernel ELF"
            )
        folded = fold_dump(
            selected,
            symbols,
            args.group_by,
            args.show_offsets,
            not args.exclude_user,
        )
        if not folded:
            raise PerfFormatError("the selected dump contains no requested samples")
        write_folded(args.out, folded)

        print(
            f"wrote {args.out} from dump {dump_index}/{len(dumps) - 1} "
            f"({selected.samples} samples, {len(selected.records)} stacks, "
            f"{selected.dropped} dropped, session={selected.session}, "
            f"reason={selected.reason})"
        )
        if not args.no_svg:
            svg = args.svg or args.out.with_suffix(".svg")
            subtitle = (
                f"session={selected.session}, reason={selected.reason}, "
                f"samples={selected.samples}, "
                f"dropped={selected.dropped}"
            )
            write_svg(svg, folded, args.title, subtitle)
            print(f"wrote {svg}")
    except (OSError, PerfFormatError) as exc:
        raise SystemExit(f"error: {exc}") from exc


if __name__ == "__main__":
    main()
