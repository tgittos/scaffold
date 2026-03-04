"""Context-Bench prompt builder."""

from __future__ import annotations

from pathlib import Path

_PROMPTS_DIR = Path(__file__).parent.parent.parent / "prompts"


def build_prompt(question: str) -> str:
    """Build system prompt for a Context-Bench question.

    Reads the template from prompts/contextbench_system.txt and fills
    in the question text.
    """
    template = (_PROMPTS_DIR / "contextbench_system.txt").read_text()
    return template.replace("{question}", question)
