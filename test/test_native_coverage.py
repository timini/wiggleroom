#!/usr/bin/env python3
"""
Coverage guard for the native C++ test executables.

The per-module drivers (test_euclogic.py, test_octolfo.py) assert specific
behaviour, but they only exercise the subcommands someone remembered to wire up.
Before this file existed, 13 of euclogic_test's commands and 2 of octolfo_test's
were never run by anything, so a regression in them was invisible even once the
executables were running in CI.

This test discovers every `cmd == "--test-..."` handler directly in the C++
source and runs it. New commands are therefore covered automatically, and the
coverage cannot silently drift again.

What it asserts per command:
  - exit code 0
  - stdout parses as JSON
  - if the payload reports "failed", it is 0
  - if the payload reports "PASS", it is truthy

Run:  python3 test/test_native_coverage.py
      pytest test/test_native_coverage.py -v
"""

import json
import re
import subprocess
import sys
from pathlib import Path

project_root = Path(__file__).resolve().parent.parent

# source file -> built executable
SUITES = {
    "euclogic_test.cpp": "euclogic_test",
    "octolfo_test.cpp": "octolfo_test",
}

# Commands that are not self-checking assertions and only dump data. They still
# must run and emit valid JSON; they simply have no pass/fail field to check.
TIMEOUT_SECONDS = 120


def find_executable(name: str) -> Path:
    for candidate in (project_root / "build" / "test" / name,
                      project_root / "build" / name):
        if candidate.exists():
            return candidate
    raise FileNotFoundError(
        f"{name} not found. Run 'just build' first to compile the test executables."
    )


def discover_commands(source_name: str) -> list[str]:
    """Pull every --test-* subcommand straight out of the C++ dispatcher."""
    src = (project_root / "test" / source_name).read_text()
    # Capture the whole quoted value rather than assuming a character class.
    # An earlier lowercase-only pattern silently skipped
    # --test-getHit-after-construct, which is exactly the kind of gap this file
    # exists to prevent.
    cmds = sorted(set(re.findall(r'cmd == "(--test-[^"]+)"', src)))
    if not cmds:
        raise AssertionError(f"No --test-* commands discovered in {source_name}")
    return cmds


def run_command(exe: Path, cmd: str) -> tuple[int, str]:
    result = subprocess.run(
        [str(exe), cmd], capture_output=True, text=True, timeout=TIMEOUT_SECONDS
    )
    return result.returncode, result.stdout


def check(exe: Path, cmd: str) -> str | None:
    """Return an error string, or None if the command is fine."""
    try:
        code, stdout = run_command(exe, cmd)
    except subprocess.TimeoutExpired:
        return f"timed out after {TIMEOUT_SECONDS}s"

    if code != 0:
        return f"exit code {code}"

    first_line = stdout.strip().split("\n")[0] if stdout.strip() else ""
    if not first_line:
        return "produced no output"

    try:
        payload = json.loads(first_line)
    except json.JSONDecodeError as exc:
        return f"first stdout line is not JSON ({exc}): {first_line[:80]}"

    if isinstance(payload, dict):
        if payload.get("failed", 0) not in (0, False):
            return f"reported failed={payload['failed']}"
        if "PASS" in payload and not payload["PASS"]:
            return "reported PASS=0"

    return None


def test_every_native_command_runs_and_passes():
    """Every discovered subcommand must run clean."""
    failures = []
    for source_name, exe_name in SUITES.items():
        exe = find_executable(exe_name)
        for cmd in discover_commands(source_name):
            problem = check(exe, cmd)
            if problem:
                failures.append(f"{exe_name} {cmd}: {problem}")
    assert not failures, "Native test commands failed:\n  " + "\n  ".join(failures)


def main() -> int:
    print("Native Test Command Coverage")
    print("=" * 60)

    total = 0
    failures = []
    for source_name, exe_name in SUITES.items():
        try:
            exe = find_executable(exe_name)
        except FileNotFoundError as exc:
            print(f"\nERROR: {exc}")
            return 1

        commands = discover_commands(source_name)
        print(f"\n{exe_name}: {len(commands)} commands discovered in {source_name}")
        for cmd in commands:
            total += 1
            problem = check(exe, cmd)
            if problem:
                failures.append(f"{exe_name} {cmd}: {problem}")
                print(f"  FAIL: {cmd} ({problem})")
            else:
                print(f"  PASS: {cmd}")

    print("\n" + "=" * 60)
    print(f"Results: {total - len(failures)}/{total} commands passed, {len(failures)} failed")
    print("=" * 60)

    if failures:
        print("\nFailures:")
        for failure in failures:
            print(f"  {failure}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
