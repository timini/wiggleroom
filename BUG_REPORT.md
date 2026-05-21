# Bug Report: Initial Repository Triage

Date: 2026-05-21

## Scope
I ran a quick bug sweep focused on the Python test harness and unit tests.

## Bug 1 — Python unit tests fail from repo root due to import path assumptions

### Severity
Medium (breaks common test invocation pattern and CI portability)

### Symptoms
Running pytest from the repository root fails during test collection with `ModuleNotFoundError` for local test modules:
- `No module named 'utils'`
- `No module named 'audio_quality'`

### Reproduction
From repo root:

```bash
python -m pytest -q test/test_utils_unit.py test/test_audio_quality_unit.py
```

### Actual result
Collection fails with import errors.

### Expected result
Tests should import their dependencies and run successfully from repo root without extra environment setup.

### Root cause
Several test files import helper modules as top-level imports (`from utils import ...`, `from audio_quality import ...`) even though those modules live under the `test/` directory. When running pytest from the repo root, `test/` is not automatically placed on `sys.path`, so imports fail.

### Evidence
- `test/test_utils_unit.py` imports from `utils`.
- `test/test_audio_quality_unit.py` imports from `audio_quality`.
- Running with an explicit path fix works:

```bash
PYTHONPATH=test python -m pytest -q test/test_utils_unit.py test/test_audio_quality_unit.py
```

(63 tests pass under this invocation.)

### Suggested fixes
Pick one:
1. Convert imports to package-qualified imports (e.g., `from test.utils import ...`) and run tests as a package.
2. Add a `conftest.py` that appends the test directory to `sys.path` for local test execution.
3. Add pytest configuration (`pythonpath = test`) in `pyproject.toml` / `pytest.ini`.
4. Document the required environment variable (`PYTHONPATH=test`) in testing docs as a short-term workaround.

---

## Notes
No code changes were made beyond this report file.
