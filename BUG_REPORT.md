# Bug Report (2026-05-21)

## Scope
- Attempted to run a representative subset of the Python test suite from the repository root.

## Findings

### 1) Python test imports fail from repository root
**Severity:** High (blocks Python test suite execution in normal root-level workflows/CI)

**Reproduction command**
```bash
python3 -m pytest -q test/test_audio_quality_unit.py test/test_utils_unit.py test/test_manifest_verification.py
```

**Observed behavior**
- `test/test_audio_quality_unit.py` fails during collection with `ModuleNotFoundError: No module named 'audio_quality'`.
- `test/test_utils_unit.py` fails during collection with `ModuleNotFoundError: No module named 'utils'`.

**Likely root cause**
- Tests import modules using top-level names (`from audio_quality import ...`, `from utils import ...`) while those modules live under the `test/` directory.
- When running pytest from repository root, `test/` is not on `PYTHONPATH`, so these imports cannot be resolved.

**Suggested fixes**
- Option A (recommended): Use package-qualified imports in tests:
  - `from test.audio_quality import ...`
  - `from test.utils import ...`
- Option B: Add pytest configuration (e.g., in `pyproject.toml` or `pytest.ini`) to include `test/` in `pythonpath`.
- Option C: Run tests via wrapper that sets `PYTHONPATH=test`, and document this as the supported invocation.

## Notes
- `test/test_manifest_verification.py` was included in the same command but did not run because pytest stopped at collection errors in the first two modules.
