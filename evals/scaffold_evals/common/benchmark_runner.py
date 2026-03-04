"""Common benchmark runner logic shared across swebench, feabench, and contextbench."""

from __future__ import annotations

import argparse
import json
import sys
import traceback
from pathlib import Path
from typing import Callable

from scaffold_evals.common.config import load_config
from scaffold_evals.common.instance_loader import load_instances, setup_repo
from scaffold_evals.common.patch_extractor import extract_patch
from scaffold_evals.common.scaffold_runner import run_scaffold


def run_patch_instance(
    instance: dict,
    scaffold_binary: str,
    model: str,
    workdir: Path,
    timeout: int,
    env_vars: dict[str, str],
    benchmark_name: str,
    message: str,
    prompt_builder: Callable[[str], str],
    scaffold_home: Path | None = None,
    debug: bool = False,
) -> dict:
    """Run scaffold on a patch-based benchmark instance (swebench/feabench).

    Returns a prediction dict with instance_id, model_name_or_path, model_patch.
    """
    instance_id = instance["instance_id"]
    print(f"[{benchmark_name}] Running {instance_id}...", flush=True)

    repo_dir = setup_repo(instance, workdir)

    issue_text = instance.get("problem_statement", "")
    system_prompt = prompt_builder(issue_text)

    home_dir = workdir / "homes" / instance_id.replace("/", "__")
    result = run_scaffold(
        message=message,
        working_dir=repo_dir,
        system_prompt=system_prompt,
        scaffold_binary=scaffold_binary,
        model=model,
        home_dir=home_dir,
        scaffold_home=scaffold_home,
        timeout=timeout,
        env_vars=env_vars,
        debug=debug,
    )

    patch = extract_patch(repo_dir)

    print(
        f"[{benchmark_name}] {instance_id}: exit={result.exit_code}, "
        f"patch_lines={len(patch.splitlines())}",
        flush=True,
    )

    if result.exit_code != 0 and result.raw_stderr:
        print(f"[{benchmark_name}] {instance_id} stderr: {result.raw_stderr[:500]}", flush=True)

    # Save scaffold logs for debugging
    log_dir = workdir / "logs" / instance_id.replace("/", "__")
    log_dir.mkdir(parents=True, exist_ok=True)
    if result.raw_stdout:
        (log_dir / "stdout.log").write_text(result.raw_stdout)
    if result.raw_stderr:
        (log_dir / "stderr.log").write_text(result.raw_stderr)

    return {
        "instance_id": instance_id,
        "model_name_or_path": model,
        "model_patch": patch,
    }


def run_patch_benchmark(
    benchmark_name: str,
    default_dataset: str,
    default_timeout: int,
    default_workdir: str,
    message: str,
    prompt_builder: Callable[[str], str],
) -> None:
    """Main entry point for patch-based benchmarks (swebench/feabench)."""
    parser = argparse.ArgumentParser(
        description=f"Run {benchmark_name} evaluation with scaffold"
    )
    parser.add_argument("--scaffold-binary", default=None, help="Path to scaffold binary")
    parser.add_argument("--model", default=None, help="Model to evaluate")
    parser.add_argument("--output", default="predictions.jsonl", help="Output predictions JSONL")
    parser.add_argument("--workdir", default=default_workdir, help="Working directory for repos")
    parser.add_argument("--dataset", default=default_dataset, help="HuggingFace dataset name")
    parser.add_argument("--split", default="test", help="Dataset split")
    parser.add_argument("--instance-ids", nargs="*", help="Specific instance IDs to run")
    parser.add_argument("--max-instances", type=int, help="Maximum number of instances")
    parser.add_argument(
        "--timeout", type=int, default=default_timeout, help="Per-instance timeout in seconds"
    )
    parser.add_argument("--config", type=Path, help="TOML config file")
    parser.add_argument(
        "--debug", action="store_true",
        help="Run scaffold in debug mode (enables verbose logging)",
    )
    parser.add_argument(
        "--scaffold-home", type=Path,
        default=Path.home() / ".local" / "scaffold",
        help="Scaffold home dir to sync OAuth credentials from (default: ~/.local/scaffold)",
    )
    args = parser.parse_args()

    config = load_config(args.config)
    scaffold_binary = args.scaffold_binary if args.scaffold_binary is not None else config.scaffold_binary
    model = args.model if args.model is not None else config.model
    workdir = Path(args.workdir)
    workdir.mkdir(parents=True, exist_ok=True)
    scaffold_home = args.scaffold_home if any((args.scaffold_home / f).exists() for f in ("oauth2.db", "config.json")) else None

    instances = load_instances(
        args.dataset,
        split=args.split,
        instance_ids=args.instance_ids,
        max_instances=args.max_instances,
    )

    if not instances:
        print(f"[{benchmark_name}] No instances to run.", file=sys.stderr)
        sys.exit(1)

    print(f"[{benchmark_name}] Running {len(instances)} instances with model={model}", flush=True)

    output_path = Path(args.output)
    if output_path.exists():
        print(f"[{benchmark_name}] Warning: {output_path} exists, overwriting.", file=sys.stderr)
        output_path.write_text("")

    for instance in instances:
        try:
            prediction = run_patch_instance(
                instance=instance,
                scaffold_binary=scaffold_binary,
                model=model,
                workdir=workdir,
                timeout=args.timeout,
                env_vars=config.env_vars,
                benchmark_name=benchmark_name,
                message=message,
                prompt_builder=prompt_builder,
                scaffold_home=scaffold_home,
                debug=args.debug,
            )
            with open(output_path, "a") as f:
                f.write(json.dumps(prediction) + "\n")
        except Exception:
            print(
                f"[{benchmark_name}] ERROR on {instance.get('instance_id', '?')}:",
                file=sys.stderr,
            )
            traceback.print_exc()

    print(f"[{benchmark_name}] Done. Predictions written to {output_path}", flush=True)
