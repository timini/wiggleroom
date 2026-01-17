#!/usr/bin/env python3
"""
Verifier Agent

Role: Build module and run comprehensive tests
Skills: Execute builds, run test suites, parse results
Output: Structured test results for the Judge Agent
"""

import json
import os
import re
import subprocess
import sys
from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import Any


def load_module_config(module_name: str, project_root: Path) -> dict:
    """Load test configuration for a module from its test_config.json file."""
    config_path = project_root / "src" / "modules" / module_name / "test_config.json"

    default_thresholds = {
        "thd_max_percent": 15.0,
        "clipping_max_percent": 1.0,
        "hnr_min_db": 0.0,
        "allow_hot_signal": False
    }

    if not config_path.exists():
        return {
            "module_type": "instrument",
            "skip_audio_tests": False,
            "thresholds": default_thresholds
        }

    try:
        with open(config_path) as f:
            data = json.load(f)

        thresholds = default_thresholds.copy()
        if "quality_thresholds" in data:
            qt = data["quality_thresholds"]
            thresholds["thd_max_percent"] = qt.get("thd_max_percent", 15.0)
            thresholds["clipping_max_percent"] = qt.get("clipping_max_percent", 1.0)
            thresholds["hnr_min_db"] = qt.get("hnr_min_db", 0.0)
            thresholds["allow_hot_signal"] = qt.get("allow_hot_signal", False)

        return {
            "module_type": data.get("module_type", "instrument"),
            "skip_audio_tests": data.get("skip_audio_tests", False),
            "skip_reason": data.get("skip_reason", ""),
            "description": data.get("description", ""),
            "thresholds": thresholds
        }
    except (json.JSONDecodeError, IOError):
        return {
            "module_type": "instrument",
            "skip_audio_tests": False,
            "thresholds": default_thresholds
        }


@dataclass
class ParameterIssue:
    """Issue found during parameter sweep."""
    param: str
    issue: str
    value: float
    severity: str = "medium"  # low, medium, high


@dataclass
class QualityMetrics:
    """Audio quality test results."""
    overall_score: int = 0
    thd_percent: float = 0.0
    thd_fundamental: float = 0.0
    aliasing_ratio_db: float = -120.0
    warmth_ratio: float = 1.0
    harmonic_character: str = "neutral"
    spectral_entropy: float = 0.0
    hnr_db: float = 0.0
    crest_factor: float = 0.0
    dynamic_range_db: float = 0.0
    attack_ms: float | None = None
    decay_ms: float | None = None
    release_ms: float | None = None
    peak_amplitude: float = 0.0
    issues: list[str] = field(default_factory=list)


@dataclass
class RenderMetrics:
    """Basic render analysis."""
    peak_amplitude: float = 0.0
    rms_level: float = 0.0
    clipping_percent: float = 0.0
    is_silent: bool = False


@dataclass
class AIAnalysis:
    """AI-based audio analysis results."""
    clap_score: int = 0
    clap_positive_sim: float = 0.0
    clap_negative_sim: float = 0.0
    top_positive: list[tuple[str, float]] = field(default_factory=list)
    top_negative: list[tuple[str, float]] = field(default_factory=list)
    gemini_quality_score: int | None = None
    gemini_musical_score: int | None = None
    gemini_issues: list[str] = field(default_factory=list)
    gemini_suggestions: list[str] = field(default_factory=list)


@dataclass
class ModuleConfig:
    """Module test configuration from test_config.json."""
    module_type: str = "instrument"
    skip_audio_tests: bool = False
    skip_reason: str = ""
    thd_max_percent: float = 15.0
    clipping_max_percent: float = 1.0
    hnr_min_db: float = 0.0
    allow_hot_signal: bool = False


@dataclass
class VerificationResult:
    """Complete verification results for a module."""
    module_name: str
    success: bool = True
    build_success: bool = True
    build_error: str = ""

    # Module config (from test_config.json)
    config: ModuleConfig = field(default_factory=ModuleConfig)

    # Basic render
    render: RenderMetrics = field(default_factory=RenderMetrics)

    # Quality tests
    quality: QualityMetrics = field(default_factory=QualityMetrics)

    # Parameter analysis
    parameter_issues: list[ParameterIssue] = field(default_factory=list)
    safe_params: list[str] = field(default_factory=list)

    # AI analysis
    ai: AIAnalysis = field(default_factory=AIAnalysis)

    def to_dict(self) -> dict:
        return asdict(self)

    def to_json(self, indent: int = 2) -> str:
        return json.dumps(self.to_dict(), indent=indent)


class VerifierAgent:
    """
    Verification Agent for Faust modules.

    Runs builds, tests, and collects structured results.
    """

    def __init__(self, project_root: Path | None = None):
        self.project_root = project_root or Path(__file__).parent.parent.parent
        self.faust_render = self.project_root / "build" / "test" / "faust_render"

    def verify(self, module_name: str,
               run_quality: bool = True,
               run_params: bool = True,
               run_ai: bool = True,
               verbose: bool = False) -> VerificationResult:
        """
        Run full verification on a module.

        Args:
            module_name: Name of the module to verify
            run_quality: Run audio quality tests
            run_params: Run parameter sweep analysis
            run_ai: Run AI analysis (CLAP)
            verbose: Print progress

        Returns:
            VerificationResult with all test data
        """
        result = VerificationResult(module_name=module_name)

        # Load module config
        config_data = load_module_config(module_name, self.project_root)
        thresholds = config_data.get("thresholds", {})
        result.config = ModuleConfig(
            module_type=config_data.get("module_type", "instrument"),
            skip_audio_tests=config_data.get("skip_audio_tests", False),
            skip_reason=config_data.get("skip_reason", ""),
            thd_max_percent=thresholds.get("thd_max_percent", 15.0),
            clipping_max_percent=thresholds.get("clipping_max_percent", 1.0),
            hnr_min_db=thresholds.get("hnr_min_db", 0.0),
            allow_hot_signal=thresholds.get("allow_hot_signal", False)
        )

        if verbose:
            print(f"[Verifier] Module config: type={result.config.module_type}, "
                  f"skip_audio={result.config.skip_audio_tests}")

        # 1. Build
        if verbose:
            print(f"[Verifier] Building {module_name}...")
        build_ok, build_err = self._run_build()
        result.build_success = build_ok
        result.build_error = build_err

        if not build_ok:
            result.success = False
            return result

        # Handle modules that skip audio tests
        if result.config.skip_audio_tests:
            if verbose:
                reason = result.config.skip_reason or "utility module"
                print(f"[Verifier] Skipping audio tests ({reason})")
            result.success = True
            return result

        # 2. Basic render test
        if verbose:
            print(f"[Verifier] Testing basic render...")
        result.render = self._test_render(module_name)

        # 3. Audio quality tests
        if run_quality:
            if verbose:
                print(f"[Verifier] Running quality tests...")
            result.quality = self._run_quality_tests(module_name)

        # 4. Parameter sweep
        if run_params:
            if verbose:
                print(f"[Verifier] Analyzing parameters...")
            issues, safe = self._analyze_parameters(module_name)
            result.parameter_issues = issues
            result.safe_params = safe

        # 5. AI analysis
        if run_ai:
            if verbose:
                print(f"[Verifier] Running AI analysis...")
            result.ai = self._run_ai_analysis(module_name)

        # Determine overall success using config thresholds
        clipping_threshold = result.config.clipping_max_percent
        if result.config.allow_hot_signal:
            clipping_threshold = max(clipping_threshold, 15.0)  # More lenient for hot signals

        result.success = (
            result.build_success and
            result.render.clipping_percent < clipping_threshold and
            not result.render.is_silent and
            result.quality.overall_score >= 70
        )

        return result

    def _run_build(self) -> tuple[bool, str]:
        """Run the build."""
        try:
            result = subprocess.run(
                ["just", "build"],
                cwd=self.project_root,
                capture_output=True,
                text=True,
                timeout=180
            )
            if result.returncode != 0:
                return False, result.stderr
            return True, ""
        except Exception as e:
            return False, str(e)

    def _test_render(self, module_name: str) -> RenderMetrics:
        """Run basic render and analyze."""
        metrics = RenderMetrics()

        try:
            result = subprocess.run(
                [str(self.faust_render), "--module", module_name,
                 "--output", "/dev/null", "--duration", "2.0",
                 "--param", "gate=5"],
                capture_output=True,
                text=True,
                timeout=30
            )

            output = result.stdout + result.stderr

            # Parse metrics
            peak_match = re.search(r'Peak amplitude:\s*([\d.]+)', output)
            if peak_match:
                metrics.peak_amplitude = float(peak_match.group(1))

            rms_match = re.search(r'RMS level:\s*([\d.]+)', output)
            if rms_match:
                metrics.rms_level = float(rms_match.group(1))

            clip_match = re.search(r'Clipped samples:.*\(([\d.]+)%\)', output)
            if clip_match:
                metrics.clipping_percent = float(clip_match.group(1))

            metrics.is_silent = metrics.rms_level < 0.001

        except Exception as e:
            pass

        return metrics

    def _run_quality_tests(self, module_name: str) -> QualityMetrics:
        """Run audio quality analysis."""
        metrics = QualityMetrics()

        try:
            result = subprocess.run(
                ["python3", "test/audio_quality.py",
                 "--module", module_name, "--json"],
                cwd=self.project_root,
                capture_output=True,
                text=True,
                timeout=60
            )

            if result.returncode == 0:
                # Parse JSON output
                try:
                    data = json.loads(result.stdout)
                    if data:
                        q = data[0] if isinstance(data, list) else data
                        metrics.overall_score = q.get("overall_score", 0)

                        if "thd" in q:
                            metrics.thd_percent = q["thd"].get("thd_percent", 0)
                            metrics.thd_fundamental = q["thd"].get("fundamental_hz", 0)

                        if "harmonics" in q:
                            metrics.warmth_ratio = q["harmonics"].get("warmth_ratio", 1)
                            metrics.harmonic_character = q["harmonics"].get("character", "neutral")

                        if "spectral" in q:
                            metrics.spectral_entropy = q["spectral"].get("entropy", 0)
                            metrics.hnr_db = q["spectral"].get("hnr_db", 0)
                            metrics.crest_factor = q["spectral"].get("crest_factor", 0)
                            metrics.dynamic_range_db = q["spectral"].get("dynamic_range_db", 0)

                        if "envelope" in q:
                            metrics.attack_ms = q["envelope"].get("attack_ms")
                            metrics.decay_ms = q["envelope"].get("decay_ms")
                            metrics.release_ms = q["envelope"].get("release_ms")
                            metrics.peak_amplitude = q["envelope"].get("peak_amplitude", 0)

                        metrics.issues = q.get("issues", [])
                except json.JSONDecodeError:
                    pass

        except Exception as e:
            pass

        return metrics

    def _analyze_parameters(self, module_name: str) -> tuple[list[ParameterIssue], list[str]]:
        """Run parameter range analysis."""
        issues = []
        safe = []

        try:
            result = subprocess.run(
                ["python3", "test/analyze_param_ranges.py",
                 "--module", module_name, "--steps", "10", "--output", "/dev/stdout"],
                cwd=self.project_root,
                capture_output=True,
                text=True,
                timeout=180
            )

            if result.returncode == 0:
                try:
                    data = json.loads(result.stdout)
                    for param in data.get("parameters", []):
                        name = param.get("name", "")
                        problems = param.get("problems", [])

                        if problems:
                            for p in problems:
                                severity = "high" if "clipping" in p.get("issue", "") else "medium"
                                issues.append(ParameterIssue(
                                    param=name,
                                    issue=p.get("issue", "unknown"),
                                    value=p.get("start", 0),
                                    severity=severity
                                ))
                        else:
                            safe.append(name)
                except json.JSONDecodeError:
                    # Parse text output instead
                    pass

        except Exception as e:
            pass

        return issues, safe

    def _run_ai_analysis(self, module_name: str) -> AIAnalysis:
        """Run AI-based audio analysis."""
        ai = AIAnalysis()

        try:
            result = subprocess.run(
                ["python3", "test/ai_audio_analysis.py",
                 "--module", module_name, "--clap-only", "--json"],
                cwd=self.project_root,
                capture_output=True,
                text=True,
                timeout=120
            )

            if result.returncode == 0:
                try:
                    data = json.loads(result.stdout)
                    if data:
                        entry = data[0] if isinstance(data, list) else data
                        clap = entry.get("clap", {})

                        ai.clap_score = int(clap.get("quality_score", 0))
                        ai.clap_positive_sim = clap.get("positive_score", 0)
                        ai.clap_negative_sim = clap.get("negative_score", 0)
                        ai.top_positive = clap.get("top_positive", [])
                        ai.top_negative = clap.get("top_negative", [])

                except json.JSONDecodeError:
                    pass

        except Exception as e:
            pass

        return ai


def main():
    """CLI for verifier agent."""
    import argparse

    parser = argparse.ArgumentParser(description="Verifier Agent - Build and test modules")
    parser.add_argument("module", help="Module to verify")
    parser.add_argument("--no-quality", action="store_true", help="Skip quality tests")
    parser.add_argument("--no-params", action="store_true", help="Skip parameter analysis")
    parser.add_argument("--no-ai", action="store_true", help="Skip AI analysis")
    parser.add_argument("--json", action="store_true", help="Output JSON")
    parser.add_argument("-v", "--verbose", action="store_true", help="Verbose output")

    args = parser.parse_args()

    agent = VerifierAgent()
    result = agent.verify(
        args.module,
        run_quality=not args.no_quality,
        run_params=not args.no_params,
        run_ai=not args.no_ai,
        verbose=args.verbose
    )

    if args.json:
        print(result.to_json())
    else:
        print(f"\n{'='*60}")
        print(f"VERIFICATION RESULTS: {result.module_name}")
        print('='*60)
        print(f"Build: {'✓' if result.build_success else '✗'}")
        print(f"Render: peak={result.render.peak_amplitude:.3f}, clipping={result.render.clipping_percent:.1f}%")
        print(f"Quality Score: {result.quality.overall_score}/100")
        print(f"AI Score (CLAP): {result.ai.clap_score}/100")

        if result.parameter_issues:
            print(f"\nParameter Issues ({len(result.parameter_issues)}):")
            for issue in result.parameter_issues:
                print(f"  - {issue.param}={issue.value}: {issue.issue} [{issue.severity}]")

        if result.quality.issues:
            print(f"\nQuality Issues:")
            for issue in result.quality.issues:
                print(f"  - {issue}")

        print(f"\nOverall: {'PASS ✓' if result.success else 'NEEDS WORK ✗'}")


if __name__ == "__main__":
    main()
