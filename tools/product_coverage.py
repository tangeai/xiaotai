#!/usr/bin/env python3
"""Measure product-core coverage using the same public-interface tests as CI."""

from __future__ import annotations

import argparse
import gzip
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
PRODUCT = ROOT / "product"
REPORT = ROOT / "tests" / ".local" / "reports" / "product-core-coverage.json"

CASES: dict[str, tuple[str, ...]] = {
    "runtime": ("xiaotai_runtime.c",),
    "call_protocol": ("xiaotai_call_protocol.c",),
    "call_state": ("xiaotai_call_state.c",),
    "call_state_resilience": ("xiaotai_call_state.c",),
    "contacts": ("xiaotai_contacts.c",),
    "ai_view": ("xiaotai_ai_view.c",),
    "ai_protocol": ("xiaotai_ai_protocol.c",),
    "video_pacer": ("xiaotai_video_pacer.c",),
    "room": ("xiaotai_room.c",),
    "room_resilience": ("xiaotai_room.c",),
    "signal": ("xiaotai_signal.c",),
    "media_contract": ("xiaotai_media_contract.c",),
    "business_scenarios": (
        "xiaotai_runtime.c",
        "xiaotai_ai_protocol.c",
        "xiaotai_call_state.c",
        "xiaotai_contacts.c",
        "xiaotai_signal.c",
        "xiaotai_media_contract.c",
    ),
}


def run(command: list[str], *, cwd: Path | None = None) -> None:
    subprocess.run(command, cwd=cwd, check=True, stdout=subprocess.DEVNULL)


def percentage(covered: int, total: int) -> float:
    return 100.0 if total == 0 else covered * 100.0 / total


def collect_coverage(work: Path) -> dict[str, object]:
    object_dir = work / "objects"
    binary_dir = work / "bin"
    gcov_dir = work / "gcov"
    object_dir.mkdir()
    binary_dir.mkdir()
    gcov_dir.mkdir()

    all_sources = sorted({source for sources in CASES.values() for source in sources})
    objects: dict[str, Path] = {}
    for source in all_sources:
        target = object_dir / f"{Path(source).stem}.o"
        run([
            "cc", "-std=c11", "-O0", "-Wall", "-Wextra", "-Werror",
            "--coverage", "-I", str(PRODUCT / "include"),
            "-I", "/usr/include/cjson", "-c",
            str(PRODUCT / "src" / source), "-o", str(target),
        ])
        objects[source] = target

    for name, sources in CASES.items():
        binary = binary_dir / name
        run([
            "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
            "-I", str(PRODUCT / "include"), "-I", "/usr/include/cjson",
            str(PRODUCT / "tests" / f"test_{name}.c"),
            *(str(objects[source]) for source in sources),
            "--coverage", "-lcjson", "-o", str(binary),
        ])
        run([str(binary)])

    for source in all_sources:
        run([
            "gcov", "--json-format", "--branch-probabilities",
            "--branch-counts",
            "--object-directory", str(object_dir),
            str(PRODUCT / "src" / source),
        ], cwd=gcov_dir)

    files: list[dict[str, object]] = []
    line_total = line_covered = branch_total = branch_covered = 0
    for result in sorted(gcov_dir.glob("*.gcov.json.gz")):
        with gzip.open(result, "rt", encoding="utf-8") as handle:
            payload = json.load(handle)
        for file_data in payload["files"]:
            source_path = Path(file_data["file"])
            if source_path.parent != PRODUCT / "src":
                continue
            lines = file_data["lines"]
            file_line_total = sum(1 for line in lines if "count" in line)
            file_line_covered = sum(
                1 for line in lines if line.get("count", 0) > 0
            )
            branches = [
                branch
                for line in lines
                for branch in line.get("branches", [])
            ]
            file_branch_total = len(branches)
            file_branch_covered = sum(
                1 for branch in branches if branch.get("count", 0) > 0
            )
            line_total += file_line_total
            line_covered += file_line_covered
            branch_total += file_branch_total
            branch_covered += file_branch_covered
            files.append({
                "file": source_path.name,
                "lines": {
                    "covered": file_line_covered,
                    "total": file_line_total,
                    "percent": round(percentage(file_line_covered,
                                                file_line_total), 2),
                },
                "branches": {
                    "covered": file_branch_covered,
                    "total": file_branch_total,
                    "percent": round(percentage(file_branch_covered,
                                                file_branch_total), 2),
                },
            })

    return {
        "lines": {
            "covered": line_covered,
            "total": line_total,
            "percent": round(percentage(line_covered, line_total), 2),
        },
        "branches": {
            "covered": branch_covered,
            "total": branch_total,
            "percent": round(percentage(branch_covered, branch_total), 2),
        },
        "files": sorted(files, key=lambda item: str(item["file"])),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--minimum-line", type=float, default=88.0)
    parser.add_argument("--minimum-branch", type=float, default=57.0)
    parser.add_argument("--report", type=Path, default=REPORT)
    args = parser.parse_args()

    if shutil.which("cc") is None or shutil.which("gcov") is None:
        raise SystemExit("product coverage requires cc and gcov")

    with tempfile.TemporaryDirectory(prefix="xiaotai-coverage-") as tmp:
        summary = collect_coverage(Path(tmp))

    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(
        json.dumps(summary, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )

    line_percent = float(summary["lines"]["percent"])
    branch_percent = float(summary["branches"]["percent"])
    print(
        "product core coverage: "
        f"lines={line_percent:.2f}% branches={branch_percent:.2f}% "
        f"report={args.report.relative_to(ROOT)}"
    )
    if line_percent < args.minimum_line or branch_percent < args.minimum_branch:
        print(
            "coverage gate failed: "
            f"minimum lines={args.minimum_line:.2f}% "
            f"branches={args.minimum_branch:.2f}%"
        )
        return 1
    critical_files = {
        "xiaotai_runtime.c": (95.0, 65.0),
        "xiaotai_media_contract.c": (100.0, 100.0),
        "xiaotai_call_state.c": (95.0, 60.0),
        "xiaotai_room.c": (80.0, 50.0),
    }
    by_name = {str(item["file"]): item for item in summary["files"]}
    for name, (minimum_line, minimum_branch) in critical_files.items():
        item = by_name.get(name)
        if item is None:
            print(f"coverage gate failed: missing critical file {name}")
            return 1
        file_line = float(item["lines"]["percent"])
        file_branch = float(item["branches"]["percent"])
        if file_line < minimum_line or file_branch < minimum_branch:
            print(
                f"coverage gate failed for {name}: "
                f"lines={file_line:.2f}% (minimum {minimum_line:.2f}%) "
                f"branches={file_branch:.2f}% "
                f"(minimum {minimum_branch:.2f}%)"
            )
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
