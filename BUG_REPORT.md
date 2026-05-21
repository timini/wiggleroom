# Bug Report

Date: 2026-05-21
Repository: `wiggleroom`

## Scope

I ran a focused set of automated tests to identify immediately reproducible failures in the current repository state.

## Findings

### 1) `test/test_utils_unit.py` fails to import `utils`

- **Severity:** High (blocks unit-test collection)
- **Symptom:** `pytest` cannot import `utils` when collecting `test/test_utils_unit.py`.
- **Error:** `ModuleNotFoundError: No module named 'utils'`
- **Likely cause:** The test imports `from utils import ...`, but `utils.py` lives in `test/utils.py`. With current invocation/environment, that module is not on `PYTHONPATH` as top-level `utils`.

#### Reproduction

```bash
pytest -q test/test_utils_unit.py test/test_manifest_verification.py
```

#### Suggested fix options

1. Update imports to package-relative form in tests, e.g. `from test.utils import ...`, or
2. Configure test environment so `test/` is added to `PYTHONPATH`, or
3. Convert helpers into an installable package/module imported consistently across test suites.

---

### 2) `tests/test_faust_output.py` contains pytest-incompatible test signatures

- **Severity:** High (test file always errors under pytest)
- **Symptom:** Functions named `test_*` require parameter `dsp_file`, but no fixture provides it.
- **Error:** `fixture 'dsp_file' not found`
- **Likely cause:** The file appears written as a script with helper functions named `test_*`, but not adapted for pytest fixtures/parameterization.

#### Reproduction

```bash
pytest -q tests/test_faust_output.py
```

#### Suggested fix options

1. Convert to proper pytest parameterization:
   - add a fixture that enumerates DSP files, and
   - parameterize `test_dsp_output` / `test_dsp_with_interpreter` over that fixture; or
2. Rename helper functions so pytest does not auto-collect them (if intended to run only via `main()` script flow).

---

## Notes

- These are deterministic, first-order failures encountered immediately during test collection/setup.
- I did not attempt code fixes in this pass; this report is intentionally focused on reproducible bug identification.
