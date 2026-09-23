# Release static checks

The Linux release pipeline runs `scripts/run_release_static_checks.sh` through
`scripts/test_acid_release.sh`, before successful build validation and artifact
upload. Every platform job must pass before publishing or submitting a release.
No workflow permission changes are needed for this integration.

Run locally from the release checkout:

```sh
RACK_DIR=/absolute/path/to/Rack-SDK scripts/run_release_static_checks.sh
python3 test/test_release_static_checks.py
```

For reviewer parity use Rack SDK 2.6.6 **lin-x64**, including on macOS. The runner
builds cppcheck 2.21.0 from pinned upstream commit
`e73bf44c3e49686b7495fab352d03a6c6075516b`. It needs curl, tar, make, Python 3 and a
C++ compiler, already available on the Linux build runner. It checks the tool
version and SDK layout and fails on tool/setup errors.

All tracked C/C++ files under `src/` are scanned, including excluded modules and
generated Faust headers. This matches the broad source scope of VCV's review of
2.1.2. Compiler definitions, C++11 analysis mode, maximum configurations and
warning selection reproduce the command in https://github.com/timini/wiggleroom/issues/125.
This is analysis mode only; the plugin continues to compile as C++17.

Cppcheck errors/warnings and pattern warnings fail the check. Cosmetic style
findings are reported without blocking. No blanket suppressions or baselines hide
existing findings. Fix or explicitly review false positives before changing policy.

Pattern rules cover missing literal `asset::plugin` resource paths, font/image
loads outside visible draw callbacks, null-module draw early returns, and possible
networking calls. These are conservative heuristics, **not** the unpublished
`rack-integration-tools` checker. Helper-function draw loads may be flagged, and
an unrelated method named `bind()` can look like networking. Dynamic asset paths
and other reviewer checks are not covered; passing is not a guarantee of acceptance.

Reports are written to `build/release-static/`: `sources.txt`, `cppcheck.xml`,
`cppcheck.stdout.txt`, `patterns.json` and `summary.txt`. The readable report is
printed to the CI log even when findings fail the job. Existing reported problems
are intentionally still blockers; adding the gate does not fix or resubmit 2.1.2.
