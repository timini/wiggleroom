# Bug Report – Repository Audit

Date: 2026-05-21

## Scope

I performed a quick bug-hunt focused on the Python-based test tooling and ran targeted tests to identify concrete failures.

## Findings

### 1) Unit tests fail to import local test modules (`utils`, `audio_quality`)

**Severity:** High (prevents unit test suite from running)

**Symptoms:**
- `pytest` fails during collection with:
  - `ModuleNotFoundError: No module named 'utils'`
  - `ModuleNotFoundError: No module named 'audio_quality'`

**Reproduction:**
```bash
cd /workspace/wiggleroom
pytest -q test/test_utils_unit.py test/test_audio_quality_unit.py
```

**Observed behavior:**
- Collection aborts before any tests execute.

**Likely root cause:**
- Tests import sibling files with top-level imports (`from utils import ...`, `from audio_quality import ...`) instead of package-aware imports.
- The directory already has `test/__init__.py`, so these should be imported as package modules (e.g., `from test.utils import ...`) or `PYTHONPATH` should be set consistently.

**Evidence in code:**
- `test/test_utils_unit.py` imports `from utils import ...`
- `test/test_audio_quality_unit.py` imports `from audio_quality import ...`
- Similar import pattern appears in multiple test scripts.

**Suggested fix options:**
1. Prefer explicit package imports throughout `test/`:
   - `from test.utils import ...`
   - `from test.audio_quality import ...`
2. Or add a `conftest.py` that adjusts `sys.path` deterministically (less preferred).
3. Ensure CI/test docs use one canonical invocation method.

---

### 2) Test documentation and direct `pytest` execution are inconsistent

**Severity:** Medium (causes developer friction and false-negative test runs)

**Symptoms:**
- The testing README emphasizes running via `just`/framework scripts.
- Running direct unit tests via `pytest` currently fails due to import layout.

**Reproduction:**
```bash
cd /workspace/wiggleroom
pytest -q test/test_utils_unit.py
```

**Observed behavior:**
- Fails with import error, even though target module (`test/utils.py`) exists.

**Likely root cause:**
- Project relies on script-style execution conventions, but unit tests are written like they can be run directly with pytest.

**Suggested fix options:**
1. Make direct pytest execution first-class by fixing imports/package layout.
2. If direct pytest is intentionally unsupported, document that explicitly and add a guard/failure message.

---

### 3) Broad systemic import fragility in test tooling

**Severity:** Medium-High (can create intermittent failures across environments)

**Symptoms:**
- Multiple scripts in `test/` use top-level imports for sibling modules:
  - `from utils import ...`
  - `from audio_quality import ...`

**Risk:**
- Behavior depends on working directory and how Python resolves `sys.path`.
- IDE runners, CI steps, and contributors may see different outcomes.

**Suggested fix:**
- Standardize imports across all `test/*.py` to package-qualified imports (`from test...`) and validate with a CI job that runs `pytest` from repo root.

## Commands Run

1. `pytest -q test/test_utils_unit.py test/test_audio_quality_unit.py test/test_report_generation.py`
2. `python test/run_tests.py --help`
3. `cd test && pytest -q test_utils_unit.py test_audio_quality_unit.py`
4. `rg "from (utils|audio_quality) import|import utils|import audio_quality" test -n`

## Recommended Next Steps

1. Refactor import statements in `test/` to package-qualified imports.
2. Add a lightweight CI check that runs at least one direct `pytest` command from repo root.
3. Update `test/README.md` with explicit supported invocation patterns.
