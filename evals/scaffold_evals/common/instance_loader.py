"""Load benchmark instances from HuggingFace datasets and set up repos."""

from __future__ import annotations

import subprocess
from pathlib import Path

from datasets import load_dataset


def load_instances(
    dataset_name: str,
    split: str = "test",
    instance_ids: list[str] | None = None,
    max_instances: int | None = None,
) -> list[dict]:
    """Load benchmark instances from a HuggingFace dataset.

    Args:
        dataset_name: HuggingFace dataset identifier.
        split: Dataset split to load.
        instance_ids: If provided, filter to only these instance IDs.
        max_instances: If provided, limit to this many instances.

    Returns:
        List of instance dicts from the dataset.
    """
    ds = load_dataset(dataset_name, split=split)
    instances = list(ds)

    if instance_ids:
        id_set = set(instance_ids)
        instances = [i for i in instances if i.get("instance_id") in id_set]

    if max_instances:
        instances = instances[:max_instances]

    return instances


# Cache of cloned repos: repo_name -> dedicated cache path
_repo_cache: dict[str, Path] = {}


def setup_repo(instance: dict, workdir: Path) -> Path:
    """Clone and reset a repository to the instance's base commit.

    Uses a dedicated cache directory for the initial clone so that
    instance working directories don't contaminate the cache.

    Args:
        instance: Benchmark instance dict with 'repo', 'base_commit' keys.
        workdir: Base directory for repo checkouts.

    Returns:
        Path to the repo directory, checked out at base_commit.
    """
    repo = instance["repo"]
    base_commit = instance["base_commit"]
    instance_id = instance["instance_id"]

    # Per-instance working copy
    instance_dir = workdir / instance_id.replace("/", "__")
    repo_dir = instance_dir / repo.replace("/", "__")

    if repo_dir.exists():
        # Reset existing checkout
        _git_reset(repo_dir, base_commit)
        return repo_dir

    # Ensure we have a cached clone to copy from
    cache_key = repo
    if cache_key not in _repo_cache:
        cache_dir = workdir / "_cache" / repo.replace("/", "__")
        if not cache_dir.exists():
            repo_url = f"https://github.com/{repo}.git"
            cache_dir.parent.mkdir(parents=True, exist_ok=True)
            subprocess.run(
                ["git", "clone", repo_url, str(cache_dir)],
                capture_output=True,
                check=True,
            )
        _repo_cache[cache_key] = cache_dir

    # Clone from cache
    cached = _repo_cache[cache_key]
    repo_dir.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        ["git", "clone", "--local", str(cached), str(repo_dir)],
        capture_output=True,
        check=True,
    )
    _git_reset(repo_dir, base_commit)
    return repo_dir


def _git_reset(repo_dir: Path, commit: str) -> None:
    """Hard-reset repo to a specific commit and clean untracked files."""
    subprocess.run(
        ["git", "checkout", "-f", commit],
        cwd=str(repo_dir),
        capture_output=True,
        check=True,
    )
    subprocess.run(
        ["git", "clean", "-fdx"],
        cwd=str(repo_dir),
        capture_output=True,
        check=True,
    )
