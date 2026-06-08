#!/usr/bin/env python3
"""Fail the build if Activity constructors use member .size() in initializer lists."""

from __future__ import annotations

import re
import sys
from pathlib import Path

PATTERN = re.compile(
    r"^\s*\w+\([^)]*%\s*(\w+)\.size\(\)",
    re.MULTILINE,
)

SUSPICIOUS_SUFFIXES = ("Labels", "Items", "Options", "Entries", "menuItems")


def scan_file(cpp_path: Path, root: Path) -> list[str]:
    text = cpp_path.read_text(encoding="utf-8")
    if not cpp_path.name.endswith("Activity.cpp"):
        return []

    header_path = cpp_path.with_suffix(".h")
    header_text = header_path.read_text(encoding="utf-8") if header_path.exists() else ""

    issues: list[str] = []
    for match in PATTERN.finditer(text):
        ident = match.group(1)
        line = match.group(0).strip()
        if "std::vector" in header_text and ident in header_text:
            issues.append(f"{cpp_path.relative_to(root)}: {line}")
            continue
        if any(ident.endswith(suffix) for suffix in SUSPICIOUS_SUFFIXES):
            issues.append(f"{cpp_path.relative_to(root)}: {line}")
    return issues


def run_check(root: Path) -> int:
    src = root / "src"
    all_issues: list[str] = []
    for cpp_path in sorted(src.rglob("*Activity.cpp")):
        all_issues.extend(scan_file(cpp_path, root))

    if all_issues:
        print("Member .size() in constructor initializer lists (use static constexpr counts instead):", file=sys.stderr)
        for issue in all_issues:
            print(f"  {issue}", file=sys.stderr)
        return 1

    print("check_member_init_order: OK")
    return 0


try:
    Import("env")
    if run_check(Path(env["PROJECT_DIR"])) != 0:
        env.Exit(1)
except NameError:
    if __name__ == "__main__":
        raise SystemExit(run_check(Path(__file__).resolve().parent.parent))
