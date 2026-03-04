"""FEA-Bench prompt builder."""

from __future__ import annotations

from pathlib import Path

_PROMPTS_DIR = Path(__file__).parent.parent.parent / "prompts"


def build_prompt(issue_text: str) -> str:
    """Build system prompt for a FEA-Bench instance.

    Reads the template from prompts/feabench_system.txt and fills
    in the issue text.
    """
    template = (_PROMPTS_DIR / "feabench_system.txt").read_text()
    return template.replace("{issue_text}", issue_text)
