"""
Faust Module Development Automation Tools

Python automation for the iterative Faust DSP development feedback loop:
- VerifierAgent: Runs builds and tests, collects metrics
- JudgeAgent: Evaluates metrics and suggests fixes
- Orchestrator: Coordinates the verification/judgment cycle

Note: These are automation tools, not Claude Code subagents.
The actual development work is done by Claude or a human developer.
"""

from .verifier_agent import VerifierAgent
from .judge_agent import JudgeAgent
from .orchestrator import Orchestrator, run_development_loop

__all__ = [
    'VerifierAgent',
    'JudgeAgent',
    'Orchestrator',
    'run_development_loop'
]
