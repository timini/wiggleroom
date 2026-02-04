#!/usr/bin/env python3
"""Print CI quality gate summary from result files."""

import json
import sys
from pathlib import Path


def main():
    # Audio quality results
    quality_file = Path("test/ci_quality_results.json")
    if quality_file.exists():
        print("Audio Quality:")
        with open(quality_file) as f:
            results = json.load(f)
        passed = sum(1 for r in results if r.get("overall_quality_score", 0) >= 70)
        total = len(results)
        print(f"  {passed}/{total} modules pass quality threshold")
    else:
        print("Audio Quality: (no results)")

    # CLAP results
    clap_file = Path("test/ci_clap_results.json")
    if clap_file.exists():
        print("CLAP Analysis:")
        with open(clap_file) as f:
            results = json.load(f)
        passed = sum(1 for r in results if r.get("clap", {}).get("quality_score", 0) >= 50)
        total = len(results)
        print(f"  {passed}/{total} modules pass CLAP threshold")
    else:
        print("CLAP Analysis: (no results)")


if __name__ == "__main__":
    main()
