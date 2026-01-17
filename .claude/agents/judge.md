# Judge Agent

Evaluate verification results and generate prioritized fix instructions.

## When to Use

Use this agent when you need to:
- Evaluate test results from the Verifier agent
- Determine if a module passes quality standards
- Get prioritized list of issues to fix
- Generate actionable fix instructions with Faust code hints

## Capabilities

- **Result evaluation**: Analyzes VerificationResult for issues
- **Severity classification**: Categorizes issues as CRITICAL, HIGH, MEDIUM, LOW
- **Fix prioritization**: Orders issues by severity and impact
- **Code hints**: Provides Faust DSP code snippets to fix common issues
- **Verdict generation**: PASS, NEEDS_WORK, or FAIL

## Usage

```bash
# Evaluate a module and get judgment
python3 test/agents/judge_agent.py ModuleName

# JSON output
python3 test/agents/judge_agent.py ModuleName --json

# Generate fix instructions (markdown format)
just agent-fix ModuleName
```

## Severity Levels

| Severity | Action | Examples |
|----------|--------|----------|
| **CRITICAL** | Fix immediately | Build failure, silent output, severe clipping (>15%) |
| **HIGH** | Fix before release | Major clipping (5-15%), very low quality score (<60) |
| **MEDIUM** | Should fix | Parameter issues, AI-detected problems, low HNR |
| **LOW** | Optional | Minor suggestions, optimization opportunities |

## Output Structure

The judge returns a `JudgmentResult` with:

```json
{
  "module_name": "ChaosFlute",
  "verdict": "needs_work",
  "overall_score": 68,
  "issues": [
    {
      "severity": "high",
      "category": "clipping",
      "description": "Clipping at 8.5% exceeds threshold of 5.0%",
      "fix_hint": "Add soft limiter or reduce output gain"
    }
  ],
  "fix_instructions": "..."
}
```

## Fix Instructions Format

When verdict is not PASS, the judge generates fix instructions:

```markdown
# Fix Instructions for ChaosFlute

DSP File: `src/modules/ChaosFlute/chaos_flute.dsp`

## Priority Issues to Fix

### 1. [CRITICAL] clipping
**Issue:** Severe clipping: 32.0% of samples
**Fix:** Add frequency-dependent gain compensation
**Faust hint:**
```faust
freq_compensation = min(1.0, freq / 261.62);
signal = raw_signal * freq_compensation;
```
```

## Common Fix Patterns

The judge provides these common fixes:

**Clipping:**
```faust
output_gain = 0.5;
output = signal : ma.tanh : *(output_gain);
```

**Transient clicks:**
```faust
gate_smooth = gate : si.smooth(0.995);
```

**DC offset:**
```faust
output = signal : fi.dcblocker;
```

**Silent output:**
```faust
min_excitation = 0.05;
excitation = min_excitation + param * (max_excitation - min_excitation);
```

## Integration

This agent is typically called after the Verifier agent:

```
Verifier → Judge → Faust Dev Agent
```

The development loop:
1. Verifier runs tests → VerificationResult
2. Judge evaluates → JudgmentResult with fix instructions
3. Faust Dev Agent applies fixes
4. Loop until verdict is PASS

## Programmatic Usage

```python
from test.agents.judge_agent import JudgeAgent
from test.agents.verifier_agent import VerifierAgent

verifier = VerifierAgent()
judge = JudgeAgent()

result = verifier.verify("ChaosFlute")
judgment = judge.evaluate(result)

if judgment.verdict != "pass":
    print(judgment.fix_instructions)
```
