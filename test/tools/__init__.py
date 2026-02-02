"""
Faust Module Development Tools

Automation tools for iterative Faust DSP development:
- Verifier: Builds modules and runs comprehensive tests
- Judge: Evaluates results and generates fix suggestions
- Orchestrator: Coordinates the verification/judgment loop

These are tools that Claude uses during the development workflow,
not Claude Code subagents.
"""

from .verifier import Verifier
from .judge import Judge
from .orchestrator import Orchestrator, run_development_loop

__all__ = [
    'Verifier',
    'Judge',
    'Orchestrator',
    'run_development_loop'
]
