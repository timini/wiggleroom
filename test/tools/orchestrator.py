#!/usr/bin/env python3
"""
Development Loop Orchestrator

Coordinates the Faust module development feedback loop:
1. Verifier builds and tests the module
2. Judge evaluates results and suggests fixes
3. Claude (the actual agent) reads output and applies fixes
4. Loop repeats until quality thresholds are met

This is a coordination tool, not a Claude Code subagent.
"""

import json
import sys
from dataclasses import dataclass, field, asdict
from datetime import datetime
from pathlib import Path
from typing import Any

from .verifier import Verifier, VerificationResult
from .judge import Judge, JudgmentResult, Verdict, Severity


@dataclass
class IterationRecord:
    """Record of a single iteration."""
    iteration: int
    timestamp: str
    verification_score: int
    judgment_score: int
    verdict: str
    issues_count: int
    high_priority_issues: list[str]
    action_taken: str = ""


@dataclass
class DevelopmentSession:
    """Complete development session record."""
    module_name: str
    started_at: str
    completed_at: str = ""
    total_iterations: int = 0
    final_verdict: str = ""
    final_score: int = 0
    iterations: list[IterationRecord] = field(default_factory=list)
    success: bool = False

    def to_dict(self) -> dict:
        return asdict(self)

    def to_json(self, indent: int = 2) -> str:
        return json.dumps(self.to_dict(), indent=indent)


@dataclass
class FixInstructions:
    """Instructions for the Faust Dev Agent."""
    module_name: str
    dsp_file: str
    priority_issues: list[dict]
    faust_hints: list[str]
    suggested_changes: list[str]
    context: str = ""

    def to_prompt(self) -> str:
        """Generate a prompt for Claude to fix the issues."""
        lines = [
            f"# Fix Instructions for {self.module_name}",
            "",
            f"DSP File: `{self.dsp_file}`",
            "",
            "## Priority Issues to Fix",
            ""
        ]

        for i, issue in enumerate(self.priority_issues, 1):
            lines.append(f"### {i}. [{issue['severity']}] {issue['category']}")
            lines.append(f"**Issue:** {issue['description']}")
            lines.append(f"**Fix:** {issue['fix_instruction']}")
            if issue.get('faust_hint'):
                lines.append(f"**Faust hint:**")
                lines.append("```faust")
                lines.append(issue['faust_hint'])
                lines.append("```")
            lines.append("")

        if self.suggested_changes:
            lines.append("## Suggested Changes")
            for change in self.suggested_changes:
                lines.append(f"- {change}")
            lines.append("")

        if self.context:
            lines.append("## Context")
            lines.append(self.context)

        return '\n'.join(lines)


class Orchestrator:
    """
    Development Loop Orchestrator.

    Coordinates verification and judgment, tracks progress,
    and generates instructions for code fixes.
    """

    def __init__(self, project_root: Path | None = None, max_iterations: int = 10):
        self.project_root = project_root or Path(__file__).parent.parent.parent
        self.max_iterations = max_iterations
        self.verifier = Verifier(project_root)
        self.judge = Judge()

    def run_iteration(self, module_name: str, verbose: bool = False) -> tuple[JudgmentResult, VerificationResult]:
        """
        Run a single verification + judgment iteration.

        Returns:
            Tuple of (judgment, verification) results
        """
        if verbose:
            print(f"\n{'='*60}")
            print(f"Running iteration for {module_name}")
            print('='*60)

        # Verify
        verification = self.verifier.verify(module_name, verbose=verbose)

        # Judge
        judgment = self.judge.judge(verification)

        return judgment, verification

    def generate_fix_instructions(self, module_name: str,
                                  judgment: JudgmentResult) -> FixInstructions:
        """
        Generate structured fix instructions for the Faust Dev Agent.
        """
        # Find DSP file
        dsp_file = self._find_dsp_file(module_name)

        # Extract priority issues (high and critical)
        priority_issues = [
            {
                'severity': i.severity.value,
                'category': i.category,
                'description': i.description,
                'fix_instruction': i.fix_instruction,
                'faust_hint': i.faust_hint
            }
            for i in judgment.issues
            if i.severity in (Severity.CRITICAL, Severity.HIGH)
        ]

        # Collect all Faust hints
        faust_hints = [i.faust_hint for i in judgment.issues if i.faust_hint]

        # Generate suggested changes
        suggested_changes = []
        for issue in judgment.issues[:5]:  # Top 5 issues
            suggested_changes.append(f"{issue.fix_instruction} ({issue.category})")

        return FixInstructions(
            module_name=module_name,
            dsp_file=str(dsp_file) if dsp_file else "",
            priority_issues=priority_issues,
            faust_hints=faust_hints,
            suggested_changes=suggested_changes,
            context=f"Score: {judgment.overall_score}/100, Verdict: {judgment.verdict.value}"
        )

    def run_development_loop(self, module_name: str,
                             auto_fix: bool = False,
                             verbose: bool = True) -> DevelopmentSession:
        """
        Run the complete development loop.

        Args:
            module_name: Module to develop
            auto_fix: If True, automatically apply suggested fixes (not implemented)
            verbose: Print progress

        Returns:
            DevelopmentSession with complete history
        """
        session = DevelopmentSession(
            module_name=module_name,
            started_at=datetime.now().isoformat()
        )

        iteration = 0

        while iteration < self.max_iterations:
            iteration += 1

            if verbose:
                print(f"\n{'#'*60}")
                print(f"# ITERATION {iteration}/{self.max_iterations}")
                print('#'*60)

            # Run verification and judgment
            judgment, verification = self.run_iteration(module_name, verbose=verbose)

            # Record iteration
            record = IterationRecord(
                iteration=iteration,
                timestamp=datetime.now().isoformat(),
                verification_score=verification.quality.overall_score,
                judgment_score=judgment.overall_score,
                verdict=judgment.verdict.value,
                issues_count=len(judgment.issues),
                high_priority_issues=[
                    i.description for i in judgment.issues
                    if i.severity in (Severity.CRITICAL, Severity.HIGH)
                ][:3]
            )
            session.iterations.append(record)

            # Print summary
            if verbose:
                self._print_iteration_summary(judgment, verification)

            # Check if we're done
            if judgment.verdict == Verdict.PASS:
                session.success = True
                session.final_verdict = "PASS"
                session.final_score = judgment.overall_score
                if verbose:
                    print(f"\n{'*'*60}")
                    print("* MODULE PASSED ALL CHECKS!")
                    print('*'*60)
                break

            if judgment.verdict == Verdict.BUILD_FAILED:
                session.final_verdict = "BUILD_FAILED"
                if verbose:
                    print(f"\n{'!'*60}")
                    print("! BUILD FAILED - Cannot continue")
                    print('!'*60)
                break

            # Generate fix instructions
            fix_instructions = self.generate_fix_instructions(module_name, judgment)

            if verbose:
                print(f"\n{'-'*60}")
                print("FIX INSTRUCTIONS FOR FAUST DEV AGENT")
                print('-'*60)
                print(fix_instructions.to_prompt())

            if not auto_fix:
                # In manual mode, we stop here and let Claude apply fixes
                session.final_verdict = "AWAITING_FIXES"
                session.final_score = judgment.overall_score
                break

            # TODO: auto_fix implementation would go here

        session.total_iterations = iteration
        session.completed_at = datetime.now().isoformat()

        return session

    def _find_dsp_file(self, module_name: str) -> Path | None:
        """Find the DSP file for a module."""
        module_dir = self.project_root / "src" / "modules" / module_name
        if module_dir.exists():
            dsp_files = list(module_dir.glob("*.dsp"))
            if dsp_files:
                return dsp_files[0]
        return None

    def _print_iteration_summary(self, judgment: JudgmentResult,
                                  verification: VerificationResult):
        """Print a summary of the iteration."""
        print(f"\n{'-'*40}")
        print("ITERATION SUMMARY")
        print('-'*40)
        print(f"Verdict: {judgment.verdict.value}")
        print(f"Score: {judgment.overall_score}/100")
        print(f"Quality Score: {verification.quality.overall_score}/100")
        print(f"AI Score: {verification.ai.clap_score}/100")
        print(f"Issues: {len(judgment.issues)} total")

        critical = [i for i in judgment.issues if i.severity == Severity.CRITICAL]
        high = [i for i in judgment.issues if i.severity == Severity.HIGH]

        if critical:
            print(f"  - CRITICAL: {len(critical)}")
        if high:
            print(f"  - HIGH: {len(high)}")


def run_development_loop(module_name: str, max_iterations: int = 10,
                         verbose: bool = True) -> DevelopmentSession:
    """
    Convenience function to run the development loop.

    Usage:
        from test.agents import run_development_loop
        session = run_development_loop("ChaosFlute")
    """
    orchestrator = Orchestrator(max_iterations=max_iterations)
    return orchestrator.run_development_loop(module_name, verbose=verbose)


def main():
    """CLI for orchestrator."""
    import argparse

    parser = argparse.ArgumentParser(
        description="Orchestrator - Coordinate Faust module development"
    )
    parser.add_argument("module", help="Module to develop")
    parser.add_argument("--max-iterations", type=int, default=10,
                       help="Maximum iterations (default: 10)")
    parser.add_argument("--single", action="store_true",
                       help="Run single iteration only")
    parser.add_argument("--json", action="store_true", help="Output JSON")
    parser.add_argument("-v", "--verbose", action="store_true",
                       help="Verbose output")

    args = parser.parse_args()

    orchestrator = Orchestrator(max_iterations=args.max_iterations)

    if args.single:
        # Single iteration
        judgment, verification = orchestrator.run_iteration(
            args.module, verbose=args.verbose
        )

        if args.json:
            print(json.dumps({
                'verification': verification.to_dict(),
                'judgment': judgment.to_dict()
            }, indent=2))
        else:
            print(f"\nVerdict: {judgment.verdict.value}")
            print(f"Score: {judgment.overall_score}/100")

            if judgment.issues:
                print(f"\nTop Issues:")
                for i, issue in enumerate(judgment.issues[:5], 1):
                    print(f"  {i}. [{issue.severity.value}] {issue.description}")

            if judgment.verdict != Verdict.PASS:
                fix = orchestrator.generate_fix_instructions(args.module, judgment)
                print(f"\n{'='*60}")
                print(fix.to_prompt())
    else:
        # Full loop
        session = orchestrator.run_development_loop(
            args.module, verbose=not args.json
        )

        if args.json:
            print(session.to_json())
        else:
            print(f"\n{'='*60}")
            print("DEVELOPMENT SESSION COMPLETE")
            print('='*60)
            print(f"Module: {session.module_name}")
            print(f"Iterations: {session.total_iterations}")
            print(f"Final Verdict: {session.final_verdict}")
            print(f"Final Score: {session.final_score}/100")
            print(f"Success: {'Yes' if session.success else 'No'}")


if __name__ == "__main__":
    main()
