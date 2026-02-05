#!/usr/bin/env python3
"""
Audio Judge Agent

Role: Evaluate verification results and generate fix instructions
Skills: Audio quality assessment, issue prioritization, fix recommendations
Output: Verdict (pass/fail) + prioritized issues + specific fix instructions
"""

import json
from dataclasses import dataclass, field, asdict
from enum import Enum
from typing import Any

from .verifier_agent import VerificationResult, ParameterIssue


class Severity(str, Enum):
    CRITICAL = "critical"
    HIGH = "high"
    MEDIUM = "medium"
    LOW = "low"


class Verdict(str, Enum):
    PASS = "pass"
    NEEDS_WORK = "needs_work"
    MAJOR_ISSUES = "major_issues"
    BUILD_FAILED = "build_failed"


@dataclass
class Issue:
    """A prioritized issue with fix instructions."""
    severity: Severity
    category: str  # clipping, noise, envelope, parameter, quality
    description: str
    fix_instruction: str
    faust_hint: str = ""  # Specific Faust code suggestion
    auto_fix_type: str | None = None  # Auto-fixable template type, if applicable


@dataclass
class JudgmentResult:
    """Complete judgment of a module."""
    module_name: str
    verdict: Verdict = Verdict.NEEDS_WORK
    overall_score: int = 0  # 0-100
    issues: list[Issue] = field(default_factory=list)
    summary: str = ""
    next_action: str = ""  # What the dev agent should do next

    def to_dict(self) -> dict:
        result = asdict(self)
        result['verdict'] = self.verdict.value
        result['issues'] = [
            {**asdict(i), 'severity': i.severity.value}
            for i in self.issues
        ]
        return result

    def to_json(self, indent: int = 2) -> str:
        return json.dumps(self.to_dict(), indent=indent)


def classify_fix_type(category: str, description: str, severity: Severity) -> str | None:
    """
    Classify an issue to an auto-fixable template.

    Args:
        category: Issue category (clipping, noise, etc.)
        description: Full description of the issue
        severity: Severity level

    Returns:
        Fix type name if auto-fixable, None otherwise
    """
    description_lower = description.lower()
    category_lower = category.lower()

    # Clipping -> add_limiter or reduce_gain
    if "clipping" in category_lower or "clipping" in description_lower:
        if severity in (Severity.CRITICAL, Severity.HIGH):
            return "add_limiter"
        return "reduce_gain"

    # DC offset -> add_dc_blocker
    if "dc" in category_lower or "dc offset" in description_lower:
        return "add_dc_blocker"

    # Clicks, pops, transients -> smooth_gate
    if any(x in description_lower for x in ["click", "pop", "transient", "discontinuit"]):
        return "smooth_gate"

    # High THD that may be intentional -> adjust_threshold
    if "thd" in category_lower:
        if "intentional" in description_lower or "may be" in description_lower:
            return "adjust_threshold"

    # Parameter issues often need threshold adjustment
    if category_lower == "parameter" and "silent" in description_lower:
        return "adjust_threshold"

    # AI quality issues are typically complex
    if "ai_quality" in category_lower:
        # Some AI issues map to simple fixes
        if "harsh" in description_lower or "distortion" in description_lower:
            return "add_limiter"
        if "click" in description_lower or "pop" in description_lower:
            return "smooth_gate"
        # Most AI issues require manual review
        return None

    # Complex issues require manual intervention
    return None


class JudgeAgent:
    """
    Audio Quality Judge Agent.

    Evaluates verification results and generates actionable fix instructions.
    """

    # Quality thresholds
    THRESHOLDS = {
        'quality_score_pass': 80,
        'quality_score_acceptable': 60,
        'clipping_max': 0.1,  # percent
        'thd_high': 15.0,  # percent
        'thd_very_high': 30.0,
        'hnr_min': 5.0,  # dB for tonal content
        'peak_min': 0.1,  # minimum output level
        'peak_max': 0.95,  # before limiting kicks in
        'clap_good': 60,
        'clap_acceptable': 40,
    }

    def judge(self, verification: VerificationResult) -> JudgmentResult:
        """
        Evaluate verification results and generate judgment.

        Args:
            verification: Results from the Verifier Agent

        Returns:
            JudgmentResult with verdict, issues, and fix instructions
        """
        result = JudgmentResult(module_name=verification.module_name)
        issues = []

        # Check build
        if not verification.build_success:
            result.verdict = Verdict.BUILD_FAILED
            result.summary = f"Build failed: {verification.build_error}"
            result.next_action = "Fix compilation errors"
            return result

        # Analyze render
        issues.extend(self._analyze_render(verification))

        # Analyze quality
        issues.extend(self._analyze_quality(verification))

        # Analyze parameters
        issues.extend(self._analyze_parameters(verification))

        # Analyze AI feedback
        issues.extend(self._analyze_ai(verification))

        # Sort by severity
        severity_order = {
            Severity.CRITICAL: 0,
            Severity.HIGH: 1,
            Severity.MEDIUM: 2,
            Severity.LOW: 3
        }
        issues.sort(key=lambda x: severity_order[x.severity])

        result.issues = issues

        # Calculate overall score
        result.overall_score = self._calculate_score(verification, issues)

        # Determine verdict
        if not issues:
            result.verdict = Verdict.PASS
            result.summary = "Module passes all quality checks"
            result.next_action = "none"
        elif any(i.severity == Severity.CRITICAL for i in issues):
            result.verdict = Verdict.MAJOR_ISSUES
            result.summary = f"Critical issues found: {len([i for i in issues if i.severity == Severity.CRITICAL])}"
            result.next_action = "fix_critical"
        elif any(i.severity == Severity.HIGH for i in issues):
            result.verdict = Verdict.NEEDS_WORK
            result.summary = f"High priority issues: {len([i for i in issues if i.severity == Severity.HIGH])}"
            result.next_action = "fix_high_priority"
        elif result.overall_score >= self.THRESHOLDS['quality_score_pass']:
            result.verdict = Verdict.PASS
            result.summary = f"Module acceptable with minor issues ({len(issues)})"
            result.next_action = "optional_improvements"
        else:
            result.verdict = Verdict.NEEDS_WORK
            result.summary = f"Score {result.overall_score}/100 below threshold"
            result.next_action = "improve_quality"

        return result

    def _analyze_render(self, v: VerificationResult) -> list[Issue]:
        """Analyze basic render metrics."""
        issues = []
        r = v.render

        # Clipping
        if r.clipping_percent > 5.0:
            severity = Severity.CRITICAL
            issue = Issue(
                severity=severity,
                category="clipping",
                description=f"Severe clipping: {r.clipping_percent:.1f}% of samples",
                fix_instruction="Reduce output gain or add limiting before output",
                faust_hint="output_gain = current_gain * 0.5; // Reduce gain\n// Or add soft limiter: : ma.tanh",
                auto_fix_type=classify_fix_type("clipping", "severe clipping", severity)
            )
            issues.append(issue)
        elif r.clipping_percent > self.THRESHOLDS['clipping_max']:
            severity = Severity.HIGH
            issue = Issue(
                severity=severity,
                category="clipping",
                description=f"Clipping detected: {r.clipping_percent:.1f}%",
                fix_instruction="Add gain compensation or soft limiting",
                faust_hint="// Add before output:\nsoft_limit = ma.tanh;\n// Reduce gain slightly",
                auto_fix_type=classify_fix_type("clipping", "clipping detected", severity)
            )
            issues.append(issue)

        # Silent
        if r.is_silent:
            issues.append(Issue(
                severity=Severity.CRITICAL,
                category="output",
                description="Module produces no audible output",
                fix_instruction="Check signal chain - excitation may not reach resonator",
                faust_hint="// Verify signal flow:\n// 1. Check gate/trigger is working\n// 2. Check oscillator/exciter output\n// 3. Increase output_gain",
                auto_fix_type=None  # Requires manual investigation
            ))
        elif r.peak_amplitude < self.THRESHOLDS['peak_min']:
            issues.append(Issue(
                severity=Severity.HIGH,
                category="output",
                description=f"Output too quiet: peak={r.peak_amplitude:.3f}",
                fix_instruction="Increase output gain",
                faust_hint=f"output_gain = {max(2.0, 0.5/r.peak_amplitude):.1f};  // Boost output",
                auto_fix_type=None  # Requires manual investigation
            ))

        return issues

    def _analyze_quality(self, v: VerificationResult) -> list[Issue]:
        """Analyze audio quality metrics."""
        issues = []
        q = v.quality

        # THD
        if q.thd_percent > self.THRESHOLDS['thd_very_high']:
            severity = Severity.HIGH
            description = f"Very high THD: {q.thd_percent:.1f}%"
            issues.append(Issue(
                severity=severity,
                category="distortion",
                description=description,
                fix_instruction="Check for gain staging issues or unwanted saturation",
                faust_hint="// Reduce internal gain before saturation stage\n// Or add lowpass filtering",
                auto_fix_type=classify_fix_type("distortion", description, severity)
            ))
        elif q.thd_percent > self.THRESHOLDS['thd_high']:
            severity = Severity.MEDIUM
            description = f"High THD: {q.thd_percent:.1f}% (may be intentional)"
            issues.append(Issue(
                severity=severity,
                category="distortion",
                description=description,
                fix_instruction="Review if distortion is intentional for this module type",
                faust_hint="// If unintentional, reduce excitation or feedback",
                auto_fix_type=classify_fix_type("distortion", description, severity)
            ))

        # Low HNR (noisy)
        if q.hnr_db < self.THRESHOLDS['hnr_min'] and q.hnr_db > 0:
            issues.append(Issue(
                severity=Severity.MEDIUM,
                category="noise",
                description=f"Low harmonic-to-noise ratio: {q.hnr_db:.1f}dB",
                fix_instruction="Reduce noise in signal chain or increase resonator feedback",
                faust_hint="// For physical models, increase resonator feedback\n// Or reduce breath/noise component",
                auto_fix_type=None  # Noise issues typically need manual review
            ))

        # Quality score issues
        for issue_text in q.issues:
            if "THD" in issue_text:
                continue  # Already handled
            issues.append(Issue(
                severity=Severity.LOW,
                category="quality",
                description=issue_text,
                fix_instruction="Review and address as needed",
                faust_hint="",
                auto_fix_type=None
            ))

        return issues

    def _analyze_parameters(self, v: VerificationResult) -> list[Issue]:
        """Analyze parameter sweep results."""
        issues = []

        for p in v.parameter_issues:
            description = f"Parameter {p.param}={p.value}: {p.issue}"
            if p.severity == "high" or "clipping" in p.issue.lower():
                severity = Severity.HIGH
                issues.append(Issue(
                    severity=severity,
                    category="parameter",
                    description=description,
                    fix_instruction=f"Add compensation or constrain {p.param} range",
                    faust_hint=f"// Option 1: Fix the issue\n// Option 2: Constrain range:\n{p.param} = hslider(\"{p.param}\", init, {p.value + 0.1}, max, step);",
                    auto_fix_type=classify_fix_type("parameter", description, severity)
                ))
            elif p.issue == "silent":
                # Silent at zero is often expected (e.g., pressure=0)
                severity = Severity.LOW
                description = f"Parameter {p.param}={p.value} produces silence"
                issues.append(Issue(
                    severity=severity,
                    category="parameter",
                    description=description,
                    fix_instruction=f"Consider constraining {p.param} minimum if silence is unwanted",
                    faust_hint=f"// Constrain to audible range:\n{p.param} = hslider(\"{p.param}\", init, {p.value + 0.1}, max, step);",
                    auto_fix_type=classify_fix_type("parameter", description, severity)
                ))

        return issues

    def _analyze_ai(self, v: VerificationResult) -> list[Issue]:
        """Analyze AI feedback."""
        issues = []
        ai = v.ai

        # Low CLAP score
        if ai.clap_score < self.THRESHOLDS['clap_acceptable']:
            # Check what negatives are high
            top_neg = ai.top_negative[:2] if ai.top_negative else []

            description = f"Low AI quality score: {ai.clap_score}/100"
            if top_neg:
                neg_desc = ", ".join([f"{n[0]} ({n[1]:.2f})" for n in top_neg])
                description += f" - detected: {neg_desc}"

            severity = Severity.MEDIUM
            issues.append(Issue(
                severity=severity,
                category="ai_quality",
                description=description,
                fix_instruction="Address detected quality issues (harshness, noise, artifacts)",
                faust_hint="// Common fixes:\n// - Add filtering: fi.lowpass(2, 8000)\n// - Smooth transients: si.smooth(0.99)\n// - Add soft limiting: ma.tanh",
                auto_fix_type=classify_fix_type("ai_quality", description, severity)
            ))

        # Specific negative matches
        for desc, score in ai.top_negative:
            if score > 0.2:  # Strong negative match
                fix = self._get_fix_for_negative(desc)
                severity = Severity.MEDIUM if score > 0.25 else Severity.LOW
                ai_description = f"AI detected: {desc} (similarity: {score:.2f})"
                issues.append(Issue(
                    severity=severity,
                    category="ai_quality",
                    description=ai_description,
                    fix_instruction=fix['instruction'],
                    faust_hint=fix['hint'],
                    auto_fix_type=classify_fix_type("ai_quality", ai_description, severity)
                ))

        return issues

    def _get_fix_for_negative(self, negative_desc: str) -> dict:
        """Get specific fix for a negative AI descriptor."""
        fixes = {
            "harsh digital distortion": {
                "instruction": "Add filtering and reduce gain staging",
                "hint": "brightness_filter = fi.lowpass(2, 6000 + param * 4000);"
            },
            "aliasing artifacts": {
                "instruction": "Add anti-aliasing filter or oversample",
                "hint": "// Add lowpass before any nonlinearity\nanti_alias = fi.lowpass(4, ma.SR/2.5);"
            },
            "unpleasant metallic sound": {
                "instruction": "Reduce high frequency resonance, add damping",
                "hint": "damping = fi.lowpass(1, 4000);\n// Apply in feedback path"
            },
            "noisy and harsh": {
                "instruction": "Reduce noise, add filtering",
                "hint": "// Reduce noise gain\n// Add output filtering: fi.lowpass(2, 8000)"
            },
            "clicking and popping": {
                "instruction": "Smooth envelope transitions",
                "hint": "// Smooth gate/envelope:\ngate_smooth = gate : si.smooth(0.995);"
            },
            "broken audio with glitches": {
                "instruction": "Check for numerical issues, add stability protection",
                "hint": "// Add DC blocker and limiter:\n: fi.dcblocker : ma.tanh"
            },
            "low quality digital sound": {
                "instruction": "Improve overall signal chain quality",
                "hint": "// General improvements:\n// - Add subtle saturation: ef.cubicnl(0.5, 0)\n// - Add filtering\n// - Improve envelope"
            }
        }

        for key, fix in fixes.items():
            if key in negative_desc.lower():
                return fix

        return {
            "instruction": "Review signal chain for quality issues",
            "hint": "// Check gain staging, filtering, and envelope"
        }

    def _calculate_score(self, v: VerificationResult, issues: list[Issue]) -> int:
        """Calculate overall score based on metrics and issues."""
        score = 100

        # Base quality score contribution (40%)
        if v.quality.overall_score > 0:
            score = v.quality.overall_score * 0.4 + 60

        # AI score contribution (20%)
        if v.ai.clap_score > 0:
            ai_contribution = (v.ai.clap_score / 100) * 20
            score = score * 0.8 + ai_contribution

        # Deductions for issues
        for issue in issues:
            if issue.severity == Severity.CRITICAL:
                score -= 30
            elif issue.severity == Severity.HIGH:
                score -= 15
            elif issue.severity == Severity.MEDIUM:
                score -= 5
            elif issue.severity == Severity.LOW:
                score -= 2

        return max(0, min(100, int(score)))


def main():
    """CLI for judge agent."""
    import argparse

    parser = argparse.ArgumentParser(description="Judge Agent - Evaluate module quality")
    parser.add_argument("--results", help="JSON file with verification results")
    parser.add_argument("--module", help="Module to verify and judge")
    parser.add_argument("--json", action="store_true", help="Output JSON")
    parser.add_argument("-v", "--verbose", action="store_true", help="Verbose output")

    args = parser.parse_args()

    if args.module:
        # Run verification first
        from .verifier_agent import VerifierAgent
        verifier = VerifierAgent()
        verification = verifier.verify(args.module, verbose=args.verbose)
    elif args.results:
        # Load from file
        with open(args.results) as f:
            data = json.load(f)
            verification = VerificationResult(**data)
    else:
        print("Error: Specify --module or --results")
        return

    judge = JudgeAgent()
    result = judge.judge(verification)

    if args.json:
        print(result.to_json())
    else:
        print(f"\n{'='*60}")
        print(f"JUDGMENT: {result.module_name}")
        print('='*60)
        print(f"Verdict: {result.verdict.value.upper()}")
        print(f"Score: {result.overall_score}/100")
        print(f"Summary: {result.summary}")
        print(f"Next Action: {result.next_action}")

        if result.issues:
            print(f"\n{'-'*60}")
            print("ISSUES (prioritized)")
            print('-'*60)
            for i, issue in enumerate(result.issues, 1):
                print(f"\n{i}. [{issue.severity.value.upper()}] {issue.category}")
                print(f"   Issue: {issue.description}")
                print(f"   Fix: {issue.fix_instruction}")
                if issue.faust_hint and args.verbose:
                    print(f"   Hint:\n{_indent(issue.faust_hint, 6)}")


def _indent(text: str, spaces: int) -> str:
    """Indent multi-line text."""
    prefix = ' ' * spaces
    return '\n'.join(prefix + line for line in text.split('\n'))


if __name__ == "__main__":
    main()
