"""
Faust Module Development Agents

A multi-agent system for iterative Faust DSP development:
- FaustDevAgent: Writes and modifies Faust DSP code
- VerifierAgent: Builds and runs tests
- JudgeAgent: Evaluates results and generates fix instructions
- Orchestrator: Coordinates the development loop
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
