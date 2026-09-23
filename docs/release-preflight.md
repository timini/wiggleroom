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

## 2.1.3 fixes and analyzer compatibility

Use `scripts/regenerate_acid9_dsp.sh` when updating the Faust DSP. Its final step
adds explicit zero initializers to generated state fields. Rack still calls the
normal DSP initialization before processing. No uninitialized-member suppression
is used.

Cppcheck 2.21.0 cannot parse the SDK's GLEW declarations containing a parameter
named identically to its type (`GLsync GLsync`). The runner creates an analysis-only
GLEW copy with that parameter renamed to `sync`. All types, declarations and bodies
remain present; the SDK used to compile the plugin is untouched. Non-Linux platform
macros are explicitly undefined for this Linux-targeted check.

The reported Stems `bind()` was a local allocation lambda, not a networking API;
it is now named `allocateLine`. The scratch array is explicitly zero-initialized.
Missing ACID9Seq and Linkage panel files are restored to the source tree only;
the release manifest and packaged module selection remain ACID9Voice-only.

The null-module draw advisories in OctoLFO, TheCauldron and PixelProbe were reviewed:
they concern optional waveform/image displays with separate background rendering,
not a missing whole module panel. These unreleased modules remain excluded. The
advisories stay visible in reports rather than being silently suppressed.

SDK-only diagnostics are retained in the raw XML and counted separately from
plugin findings. Only diagnostics whose every location is inside the supplied
SDK or its analysis overlay are classified this way. Parsing/preprocessor/internal
errors always fail, regardless of location, to prevent partial analysis passing.
All project-source errors and warnings still block; no project baseline is used.
