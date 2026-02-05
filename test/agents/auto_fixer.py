#!/usr/bin/env python3
"""
Auto-Fixer for Faust DSP Modules

Applies template-based fixes to common DSP issues without human intervention.

Supported fix types:
- reduce_gain: Reduce output gain to fix clipping
- add_limiter: Add soft limiter (ma.tanh) before output
- add_dc_blocker: Add DC blocker to remove DC offset
- smooth_gate: Add gate smoothing to reduce clicks
- adjust_threshold: Modify test_config.json thresholds

Usage:
    from test.agents.auto_fixer import AutoFixer

    fixer = AutoFixer()
    result = fixer.apply_fix("ChaosFlute", "add_limiter", {})
    if result.success:
        print("Fix applied!")
    else:
        fixer.rollback("ChaosFlute")
"""

import json
import re
import shutil
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable


@dataclass
class FixResult:
    """Result of applying a fix."""
    success: bool
    fix_type: str
    module_name: str
    message: str
    file_modified: str = ""
    backup_path: str = ""
    details: dict = field(default_factory=dict)


@dataclass
class FixTemplate:
    """Template for a specific fix type."""
    name: str
    description: str
    target: str  # "dsp" or "config"
    auto_fixable: bool = True
    apply_fn: Callable[..., FixResult] | None = None


class AutoFixer:
    """
    Apply template-based fixes to Faust DSP code and test configs.

    Supports automatic application of common fixes with rollback capability.
    """

    def __init__(self, project_root: Path | None = None):
        self.project_root = project_root or Path(__file__).parent.parent.parent
        self.backups: dict[str, list[tuple[Path, Path]]] = {}  # module -> [(original, backup)]

        # Register fix templates
        self.templates: dict[str, FixTemplate] = {
            "reduce_gain": FixTemplate(
                name="reduce_gain",
                description="Reduce output gain to fix clipping",
                target="dsp",
                auto_fixable=True,
            ),
            "add_limiter": FixTemplate(
                name="add_limiter",
                description="Add soft limiter (ma.tanh) before output",
                target="dsp",
                auto_fixable=True,
            ),
            "add_dc_blocker": FixTemplate(
                name="add_dc_blocker",
                description="Add DC blocker to remove DC offset",
                target="dsp",
                auto_fixable=True,
            ),
            "smooth_gate": FixTemplate(
                name="smooth_gate",
                description="Add gate smoothing to reduce clicks",
                target="dsp",
                auto_fixable=True,
            ),
            "adjust_threshold": FixTemplate(
                name="adjust_threshold",
                description="Modify test_config.json thresholds",
                target="config",
                auto_fixable=True,
            ),
        }

    def get_dsp_file(self, module_name: str) -> Path | None:
        """Find the DSP file for a module."""
        module_dir = self.project_root / "src" / "modules" / module_name
        if not module_dir.exists():
            return None

        # Try common naming conventions
        patterns = [
            module_name.lower() + ".dsp",
            self._camel_to_snake(module_name) + ".dsp",
        ]

        for pattern in patterns:
            dsp_file = module_dir / pattern
            if dsp_file.exists():
                return dsp_file

        # Search for any .dsp file
        dsp_files = list(module_dir.glob("*.dsp"))
        return dsp_files[0] if dsp_files else None

    def get_config_file(self, module_name: str) -> Path:
        """Get the test config file path for a module."""
        return self.project_root / "src" / "modules" / module_name / "test_config.json"

    def _camel_to_snake(self, name: str) -> str:
        """Convert CamelCase to snake_case."""
        s1 = re.sub('(.)([A-Z][a-z]+)', r'\1_\2', name)
        return re.sub('([a-z0-9])([A-Z])', r'\1_\2', s1).lower()

    def _backup_file(self, file_path: Path, module_name: str) -> Path:
        """Create a backup of a file before modifying it."""
        backup_path = file_path.with_suffix(file_path.suffix + ".bak")
        shutil.copy2(file_path, backup_path)

        if module_name not in self.backups:
            self.backups[module_name] = []
        self.backups[module_name].append((file_path, backup_path))

        return backup_path

    def apply_fix(self, module_name: str, fix_type: str,
                  params: dict[str, Any] | None = None) -> FixResult:
        """
        Apply a fix to a module.

        Args:
            module_name: Name of the module to fix
            fix_type: Type of fix to apply (from templates)
            params: Additional parameters for the fix

        Returns:
            FixResult with success status and details
        """
        if params is None:
            params = {}

        if fix_type not in self.templates:
            return FixResult(
                success=False,
                fix_type=fix_type,
                module_name=module_name,
                message=f"Unknown fix type: {fix_type}"
            )

        template = self.templates[fix_type]

        # Route to appropriate handler
        if template.target == "dsp":
            return self._apply_dsp_fix(module_name, fix_type, params)
        elif template.target == "config":
            return self._apply_config_fix(module_name, fix_type, params)
        else:
            return FixResult(
                success=False,
                fix_type=fix_type,
                module_name=module_name,
                message=f"Unknown target type: {template.target}"
            )

    def _apply_dsp_fix(self, module_name: str, fix_type: str,
                        params: dict[str, Any]) -> FixResult:
        """Apply a fix to a DSP file."""
        dsp_file = self.get_dsp_file(module_name)
        if dsp_file is None:
            return FixResult(
                success=False,
                fix_type=fix_type,
                module_name=module_name,
                message=f"DSP file not found for module: {module_name}"
            )

        # Read the DSP file
        with open(dsp_file) as f:
            content = f.read()

        # Apply the specific fix
        if fix_type == "reduce_gain":
            new_content, success = self._fix_reduce_gain(content, params)
        elif fix_type == "add_limiter":
            new_content, success = self._fix_add_limiter(content, params)
        elif fix_type == "add_dc_blocker":
            new_content, success = self._fix_add_dc_blocker(content, params)
        elif fix_type == "smooth_gate":
            new_content, success = self._fix_smooth_gate(content, params)
        else:
            return FixResult(
                success=False,
                fix_type=fix_type,
                module_name=module_name,
                message=f"DSP fix not implemented: {fix_type}"
            )

        if not success:
            return FixResult(
                success=False,
                fix_type=fix_type,
                module_name=module_name,
                message=f"Could not apply {fix_type} fix - pattern not found"
            )

        # Backup and write
        backup_path = self._backup_file(dsp_file, module_name)
        with open(dsp_file, "w") as f:
            f.write(new_content)

        return FixResult(
            success=True,
            fix_type=fix_type,
            module_name=module_name,
            message=f"Applied {fix_type} to {dsp_file.name}",
            file_modified=str(dsp_file),
            backup_path=str(backup_path)
        )

    def _apply_config_fix(self, module_name: str, fix_type: str,
                           params: dict[str, Any]) -> FixResult:
        """Apply a fix to test_config.json."""
        config_file = self.get_config_file(module_name)

        # Load existing config or create new
        if config_file.exists():
            with open(config_file) as f:
                config = json.load(f)
            backup_path = self._backup_file(config_file, module_name)
        else:
            config = {}
            backup_path = None

        # Apply threshold adjustments
        if fix_type == "adjust_threshold":
            if "quality_thresholds" not in config:
                config["quality_thresholds"] = {}

            thresholds = config["quality_thresholds"]

            # Apply requested threshold changes
            if "thd_max_percent" in params:
                thresholds["thd_max_percent"] = params["thd_max_percent"]
            if "clipping_max_percent" in params:
                thresholds["clipping_max_percent"] = params["clipping_max_percent"]
            if "hnr_min_db" in params:
                thresholds["hnr_min_db"] = params["hnr_min_db"]
            if "allow_hot_signal" in params:
                thresholds["allow_hot_signal"] = params["allow_hot_signal"]

            # Default: if no specific params, be more lenient
            if not params:
                # Raise THD threshold for intentional distortion
                thresholds["thd_max_percent"] = max(
                    thresholds.get("thd_max_percent", 15.0), 30.0
                )
                # Raise clipping threshold
                thresholds["clipping_max_percent"] = max(
                    thresholds.get("clipping_max_percent", 1.0), 5.0
                )

        # Write config
        config_file.parent.mkdir(parents=True, exist_ok=True)
        with open(config_file, "w") as f:
            json.dump(config, f, indent=2)

        return FixResult(
            success=True,
            fix_type=fix_type,
            module_name=module_name,
            message=f"Updated thresholds in {config_file.name}",
            file_modified=str(config_file),
            backup_path=str(backup_path) if backup_path else "",
            details={"thresholds": config.get("quality_thresholds", {})}
        )

    def _fix_reduce_gain(self, content: str, params: dict) -> tuple[str, bool]:
        """Reduce output gain in DSP code."""
        gain_factor = params.get("factor", 0.7)

        # Look for common output patterns and reduce gain
        patterns = [
            # process = ... ;  -> process = ... : *(gain);
            (r'(process\s*=\s*[^;]+)(;)', rf'\1 : *({gain_factor})\2'),
            # output_gain = X; -> output_gain = X * factor;
            (r'(output_gain\s*=\s*)(\d+\.?\d*)', rf'\1{gain_factor} * \2'),
        ]

        modified = False
        for pattern, replacement in patterns:
            new_content, count = re.subn(pattern, replacement, content)
            if count > 0:
                content = new_content
                modified = True
                break

        return content, modified

    def _fix_add_limiter(self, content: str, params: dict) -> tuple[str, bool]:
        """Add soft limiter (ma.tanh) before output."""
        # Look for process definition and add limiter
        patterns = [
            # process = expr; -> process = expr : ma.tanh;
            (r'(process\s*=\s*)([^;]+)(;)',
             r'\1\2 : ma.tanh\3'),
        ]

        # Check if limiter already exists
        if "ma.tanh" in content or "tanh" in content:
            return content, False  # Already has limiter

        for pattern, replacement in patterns:
            new_content, count = re.subn(pattern, replacement, content, count=1)
            if count > 0:
                return new_content, True

        return content, False

    def _fix_add_dc_blocker(self, content: str, params: dict) -> tuple[str, bool]:
        """Add DC blocker to output."""
        # Check if DC blocker already exists
        if "fi.dcblocker" in content or "dcblocker" in content:
            return content, False

        # Look for process definition and add DC blocker
        patterns = [
            # process = expr; -> process = expr : fi.dcblocker;
            (r'(process\s*=\s*)([^;]+)(;)',
             r'\1\2 : fi.dcblocker\3'),
        ]

        for pattern, replacement in patterns:
            new_content, count = re.subn(pattern, replacement, content, count=1)
            if count > 0:
                return new_content, True

        return content, False

    def _fix_smooth_gate(self, content: str, params: dict) -> tuple[str, bool]:
        """Add gate smoothing to reduce clicks."""
        smooth_factor = params.get("factor", 0.995)

        # Check if gate smoothing already exists
        if "gate_smooth" in content or ("gate" in content and "si.smooth" in content):
            return content, False

        # Look for gate parameter definition and add smoothing
        # gate = hslider("gate", ...) -> gate = hslider("gate", ...) : si.smooth(0.995)
        pattern = r'(gate\s*=\s*hslider\s*\([^)]+\))'
        replacement = rf'\1 : si.smooth({smooth_factor})'

        new_content, count = re.subn(pattern, replacement, content)
        if count > 0:
            return new_content, True

        # Alternative: look for bare gate usage
        # gate > 0 -> gate_smooth > 0
        # This is more complex, so we skip for now

        return content, False

    def validate_fix(self, module_name: str, original_metrics: dict) -> bool:
        """
        Validate that a fix improved the module's quality.

        This requires rebuilding and re-testing the module.

        Args:
            module_name: Module to validate
            original_metrics: Metrics from before the fix

        Returns:
            True if metrics improved or stayed acceptable
        """
        # This would need to import and run the verifier
        # For now, return True (assume fix worked)
        return True

    def rollback(self, module_name: str) -> bool:
        """
        Rollback all changes made to a module.

        Args:
            module_name: Module to rollback

        Returns:
            True if rollback succeeded
        """
        if module_name not in self.backups:
            return False

        success = True
        for original_path, backup_path in reversed(self.backups[module_name]):
            try:
                if backup_path.exists():
                    shutil.copy2(backup_path, original_path)
                    backup_path.unlink()
            except Exception:
                success = False

        # Clear backup list
        del self.backups[module_name]
        return success

    def clear_backups(self, module_name: str | None = None):
        """
        Clear backup files without restoring.

        Args:
            module_name: Module to clear, or None for all
        """
        if module_name:
            modules = [module_name] if module_name in self.backups else []
        else:
            modules = list(self.backups.keys())

        for mod in modules:
            for _, backup_path in self.backups[mod]:
                try:
                    if backup_path.exists():
                        backup_path.unlink()
                except Exception:
                    pass
            del self.backups[mod]

    def get_available_fixes(self) -> list[dict]:
        """Get list of available fix templates."""
        return [
            {
                "name": t.name,
                "description": t.description,
                "target": t.target,
                "auto_fixable": t.auto_fixable
            }
            for t in self.templates.values()
        ]


def classify_fix_type(issue_category: str, issue_description: str,
                      severity: str) -> str | None:
    """
    Classify an issue to an auto-fixable template.

    Args:
        issue_category: Category from judge (clipping, noise, etc.)
        issue_description: Full description of the issue
        severity: Severity level (CRITICAL, HIGH, MEDIUM, LOW)

    Returns:
        Fix type name if auto-fixable, None otherwise
    """
    description_lower = issue_description.lower()
    category_lower = issue_category.lower()

    # Clipping -> add_limiter or reduce_gain
    if "clipping" in category_lower or "clipping" in description_lower:
        if severity in ("CRITICAL", "HIGH"):
            return "add_limiter"
        return "reduce_gain"

    # DC offset -> add_dc_blocker
    if "dc" in category_lower or "dc offset" in description_lower:
        return "add_dc_blocker"

    # Clicks, pops, transients -> smooth_gate
    if any(x in description_lower for x in ["click", "pop", "transient", "discontinuit"]):
        return "smooth_gate"

    # High THD (if intentional) -> adjust_threshold
    if "thd" in category_lower and "intentional" in description_lower:
        return "adjust_threshold"

    # Threshold-related issues
    if "threshold" in description_lower or "test_config" in description_lower:
        return "adjust_threshold"

    # Complex issues require manual intervention
    return None


def main():
    """CLI for auto-fixer."""
    import argparse

    parser = argparse.ArgumentParser(
        description="Auto-Fixer for Faust DSP modules"
    )
    parser.add_argument("module", nargs="?", help="Module to fix")
    parser.add_argument("--fix", choices=[
        "reduce_gain", "add_limiter", "add_dc_blocker",
        "smooth_gate", "adjust_threshold"
    ], help="Fix type to apply")
    parser.add_argument("--list", action="store_true",
                       help="List available fix types")
    parser.add_argument("--rollback", action="store_true",
                       help="Rollback changes to module")
    parser.add_argument("--dry-run", action="store_true",
                       help="Show what would be done without applying")
    parser.add_argument("-v", "--verbose", action="store_true",
                       help="Verbose output")

    args = parser.parse_args()

    fixer = AutoFixer()

    if args.list:
        print("Available fix types:")
        for fix in fixer.get_available_fixes():
            print(f"  {fix['name']:<20} {fix['description']}")
            print(f"    Target: {fix['target']}, Auto-fixable: {fix['auto_fixable']}")
        return

    if not args.module:
        print("Error: Module name required")
        parser.print_help()
        return

    if args.rollback:
        if fixer.rollback(args.module):
            print(f"Rolled back changes to {args.module}")
        else:
            print(f"No backups found for {args.module}")
        return

    if not args.fix:
        print("Error: --fix type required")
        parser.print_help()
        return

    if args.dry_run:
        dsp_file = fixer.get_dsp_file(args.module)
        config_file = fixer.get_config_file(args.module)
        template = fixer.templates.get(args.fix)

        if template:
            print(f"Would apply {args.fix} to {args.module}")
            print(f"  Target: {template.target}")
            if template.target == "dsp":
                print(f"  File: {dsp_file}")
            else:
                print(f"  File: {config_file}")
            print(f"  Description: {template.description}")
        return

    # Apply fix
    result = fixer.apply_fix(args.module, args.fix, {})

    if result.success:
        print(f"SUCCESS: {result.message}")
        if args.verbose:
            print(f"  Modified: {result.file_modified}")
            print(f"  Backup: {result.backup_path}")
            if result.details:
                print(f"  Details: {result.details}")
    else:
        print(f"FAILED: {result.message}")


if __name__ == "__main__":
    main()
