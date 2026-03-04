"""SWE-bench prompt builder."""

from __future__ import annotations

from pathlib import Path

_PROMPTS_DIR = Path(__file__).parent.parent.parent / "prompts"


def build_prompt(issue_text: str) -> str:
    """Build system prompt for a SWE-bench instance.

    Reads the template from prompts/swebench_system.txt and fills
    in the issue text.
    """
    template = (_PROMPTS_DIR / "swebench_system.txt").read_text()
    return template.replace("{issue_text}", issue_text)
