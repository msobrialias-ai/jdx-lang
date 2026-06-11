#!/usr/bin/env python3
from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import hashlib
import sys


BASE_DIR = Path(__file__).resolve().parent.parent
MODULE_DIR = BASE_DIR / "src/modules"
OUTPUT = BASE_DIR / "generated/EmbeddedModules.hpp"


@dataclass(frozen=True)
class ModuleEntry:
    key: str
    content: str


def canonical_module_key(path: Path) -> str:
    """
    Convert:
      src/modules/math.jdx        -> math
      src/modules/math/index.jdx  -> math
      src/modules/net/http.jdx    -> net/http
      src/modules/net/http/index.jdx -> net/http
    """
    rel = path.relative_to(MODULE_DIR)

    if rel.name == "index.jdx":
        key = rel.parent.as_posix()
    else:
        key = rel.with_suffix("").as_posix()

    if key == ".":
        return ""
    return key


def make_raw_string_literal(text: str, seed: str) -> str:
    """
    Create a C++ raw string literal with a delimiter that is very unlikely
    to collide with the module source.
    """
    digest = hashlib.sha256(seed.encode("utf-8")).hexdigest()[:8]
    delim = f"JDX_{digest}"

    # Safety check: extremely unlikely to fail, but keep retrying if needed.
    candidate = f')' + delim + '"'
    if candidate in text:
        for i in range(1, 100):
            alt = f"JDX_{digest}_{i}"
            if f')' + alt + '"' not in text:
                delim = alt
                break
        else:
            raise RuntimeError(f"Unable to find safe raw string delimiter for {seed}")

    return f'R"{delim}(\n{text}\n){delim}"'


def collect_modules() -> list[ModuleEntry]:
    if not MODULE_DIR.exists():
        raise FileNotFoundError(f"Module directory not found: {MODULE_DIR}")

    entries: list[ModuleEntry] = []

    for file in sorted(MODULE_DIR.rglob("*.jdx")):
        key = canonical_module_key(file)
        if not key:
            continue

        content = file.read_text(encoding="utf-8")
        entries.append(ModuleEntry(key=key, content=content))

    return entries


def render_header(entries: list[ModuleEntry]) -> str:
    lines: list[str] = []
    lines.append("#pragma once")
    lines.append("")
    lines.append("#include <string>")
    lines.append("#include <stdexcept>")
    lines.append("#include <string_view>")
    lines.append("#include <unordered_map>")
    lines.append("")
    lines.append("namespace jdx::modules {")
    lines.append("")
    lines.append("inline const std::unordered_map<std::string, std::string_view> EmbeddedModules = {")
    lines.append("")

    for entry in entries:
        literal = make_raw_string_literal(entry.content, entry.key)
        lines.append(f'    {{"{entry.key}", {literal}}},')

    lines.append("")
    lines.append("};")
    lines.append("")
    lines.append("inline bool hasEmbeddedModule(std::string_view key) {")
    lines.append("    return EmbeddedModules.find(std::string(key)) != EmbeddedModules.end();")
    lines.append("}")
    lines.append("")
    lines.append("inline std::string_view getEmbeddedModule(std::string_view key) {")
    lines.append("    auto it = EmbeddedModules.find(std::string(key));")
    lines.append("    if (it == EmbeddedModules.end()) {")
    lines.append('        throw std::runtime_error("Runtime Error: Embedded module not found.");')
    lines.append("    }")
    lines.append("    return it->second;")
    lines.append("}")
    lines.append("")
    lines.append("} // namespace jdx::modules")
    lines.append("")

    return "\n".join(lines)


def main() -> int:
    entries = collect_modules()

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(render_header(entries), encoding="utf-8")

    print(f"Generated {OUTPUT} with {len(entries)} embedded module(s).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())