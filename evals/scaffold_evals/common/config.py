"""Configuration management for scaffold evals.

Loads TOML config files with environment variable overrides.
"""

from __future__ import annotations

import os
import sys
from dataclasses import dataclass, field
from pathlib import Path

if sys.version_info >= (3, 11):
    import tomllib
else:
    import tomli as tomllib


@dataclass
class EvalConfig:
    """Evaluation configuration."""

    scaffold_binary: str = "out/scaffold"
    model: str = "claude-sonnet-4-20250514"
    timeout: int = 600
    workdir: str = "/tmp/eval"
    openai_api_key: str = ""
    anthropic_api_key: str = ""
    github_token: str = ""
    env_vars: dict[str, str] = field(default_factory=dict)


def load_config(config_path: Path | None = None) -> EvalConfig:
    """Load config from TOML file, then overlay environment variables."""
    raw: dict = {}
    if config_path and config_path.exists():
        with open(config_path, "rb") as f:
            raw = tomllib.load(f)

    config = EvalConfig(
        scaffold_binary=raw.get("scaffold_binary", EvalConfig.scaffold_binary),
        model=raw.get("model", EvalConfig.model),
        timeout=raw.get("timeout", EvalConfig.timeout),
        workdir=raw.get("workdir", EvalConfig.workdir),
    )

    # Environment variable overrides
    config.openai_api_key = os.environ.get("OPENAI_API_KEY", raw.get("openai_api_key", ""))
    config.anthropic_api_key = os.environ.get("ANTHROPIC_API_KEY", raw.get("anthropic_api_key", ""))
    config.github_token = os.environ.get("GITHUB_TOKEN", raw.get("github_token", ""))

    # Build env vars dict for subprocess
    config.env_vars = {k: v for k, v in os.environ.items()}
    if config.openai_api_key:
        config.env_vars["OPENAI_API_KEY"] = config.openai_api_key
    if config.anthropic_api_key:
        config.env_vars["ANTHROPIC_API_KEY"] = config.anthropic_api_key
    if config.github_token:
        config.env_vars["GITHUB_TOKEN"] = config.github_token

    return config
