"""LiveCodeBench evaluation runner for scaffold.

Generates code solutions for competitive programming problems from LiveCodeBench.
Uses the datasets library (pinned <4 for trust_remote_code support) with streaming
to load problems on-demand without caching multi-GB files to disk.

Output format (JSONL, one per line):
    {"question_id": "...", "code_list": ["..."], "instance_id": "...", "model_name_or_path": "..."}

The code_list format is compatible with LiveCodeBench's official evaluation tool.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import traceback
from pathlib import Path

from scaffold_evals.common.config import load_config
from scaffold_evals.common.scaffold_runner import extract_last_assistant_text, run_scaffold


def load_problems(
    dataset: str,
    version: str,
    instance_ids: list[str] | None = None,
    max_instances: int | None = None,
) -> list[dict]:
    """Load LiveCodeBench problems via HuggingFace streaming.

    Requires datasets<4 (which supports trust_remote_code for script-based datasets).
    Streaming avoids caching the multi-GB JSONL files to disk.
    """
    from datasets import load_dataset

    ds = load_dataset(
        dataset, version_tag=version, split="test",
        streaming=True, trust_remote_code=True,
    )

    id_set = set(instance_ids) if instance_ids else None
    problems: list[dict] = []

    for item in ds:
        qid = str(item.get("question_id", ""))
        if id_set and qid not in id_set:
            continue
        problems.append(dict(item))
        if max_instances and len(problems) >= max_instances:
            break

    return problems


def _format_test_cases(public_test_cases: str | list) -> str:
    """Format public test cases for the prompt."""
    if isinstance(public_test_cases, str):
        try:
            cases = json.loads(public_test_cases)
        except (json.JSONDecodeError, TypeError):
            return public_test_cases
    else:
        cases = public_test_cases

    if not cases:
        return ""

    parts = []
    for i, case in enumerate(cases, 1):
        inp = case.get("input", "")
        out = case.get("output", "")
        parts.append(f"Example {i}:\nInput:\n{inp}\nOutput:\n{out}")
    return "\n\n".join(parts)


def _build_message(problem: dict) -> str:
    """Build the scaffold prompt for a LiveCodeBench problem."""
    title = problem.get("question_title", "Untitled")
    content = problem.get("question_content", "")
    starter_code = problem.get("starter_code", "")
    public_tests = problem.get("public_test_cases", "")
    difficulty = problem.get("difficulty", "")
    platform = problem.get("platform", "")

    meta_parts = []
    if platform:
        meta_parts.append(f"Platform: {platform}")
    if difficulty:
        meta_parts.append(f"Difficulty: {difficulty}")
    meta_line = " | ".join(meta_parts)

    formatted_tests = _format_test_cases(public_tests)

    parts = [
        f"Solve the following competitive programming problem.\n",
        f"## {title}\n",
    ]
    if meta_line:
        parts.append(f"{meta_line}\n")
    parts.append(f"### Problem\n\n{content}\n")

    if formatted_tests:
        parts.append(f"### Examples\n\n{formatted_tests}\n")

    if starter_code and starter_code.strip():
        parts.append(
            f"### Starter Code\n\n```python\n{starter_code.strip()}\n```\n\n"
            "Complete the implementation of the function above.\n"
        )
    else:
        parts.append(
            "Write a complete Python solution that reads from stdin and "
            "prints to stdout.\n"
        )

    parts.append(
        "\n### Instructions\n\n"
        "- Write correct, efficient Python code.\n"
        "- Handle all edge cases from the problem constraints.\n"
        "- Output ONLY the final solution code in a single fenced code block.\n"
        "- Do not include explanations outside the code block.\n"
    )

    return "\n".join(parts)


def _extract_code(text: str) -> str:
    """Extract code from scaffold's response.

    Looks for fenced code blocks first, falls back to the full text.
    """
    pattern = r"```(?:python|py)?\s*\n(.*?)```"
    matches = re.findall(pattern, text, re.DOTALL)
    if matches:
        return matches[-1].strip()
    return text.strip()


def run_problem(
    problem: dict,
    scaffold_binary: str,
    model: str,
    workdir: Path,
    timeout: int,
    env_vars: dict[str, str],
    scaffold_home: Path | None = None,
    debug: bool = False,
) -> dict:
    """Run scaffold on a single LiveCodeBench problem."""
    qid = str(problem["question_id"])
    title = problem.get("question_title", "")
    print(f"[livecodebench] Running {qid} ({title})...", flush=True)

    home_dir = workdir / "homes" / qid
    problem_dir = workdir / "problems" / qid
    problem_dir.mkdir(parents=True, exist_ok=True)

    message = _build_message(problem)

    result = run_scaffold(
        message=message,
        working_dir=problem_dir,
        scaffold_binary=scaffold_binary,
        model=model,
        home_dir=home_dir,
        scaffold_home=scaffold_home,
        timeout=timeout,
        env_vars=env_vars,
        debug=debug,
    )

    answer = extract_last_assistant_text(result.messages)
    code = _extract_code(answer)

    print(
        f"[livecodebench] {qid}: exit={result.exit_code}, "
        f"code_lines={len(code.splitlines())}",
        flush=True,
    )

    log_dir = workdir / "logs" / qid
    log_dir.mkdir(parents=True, exist_ok=True)
    if result.raw_stdout:
        (log_dir / "stdout.log").write_text(result.raw_stdout)
    if result.raw_stderr:
        (log_dir / "stderr.log").write_text(result.raw_stderr)

    return {
        "instance_id": qid,
        "question_id": qid,
        "model_name_or_path": model,
        "code_list": [code],
        "exit_code": result.exit_code,
    }


DEFAULT_DATASET = "livecodebench/code_generation_lite"
DEFAULT_VERSION = "release_v6"


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Run LiveCodeBench evaluation with scaffold"
    )
    parser.add_argument("--scaffold-binary", default=None, help="Path to scaffold binary")
    parser.add_argument("--model", default=None, help="Model to evaluate")
    parser.add_argument("--output", default="predictions.jsonl", help="Output JSONL")
    parser.add_argument(
        "--dataset", default=DEFAULT_DATASET, help="HuggingFace dataset name"
    )
    parser.add_argument(
        "--version", default=DEFAULT_VERSION,
        help=f"Dataset version tag (default: {DEFAULT_VERSION}). Available: release_v1 through release_v6",
    )
    parser.add_argument(
        "--workdir", default="/tmp/eval/livecodebench", help="Working directory"
    )
    parser.add_argument("--instance-ids", nargs="*", help="Specific question IDs to run")
    parser.add_argument("--max-instances", type=int, help="Maximum number of problems")
    parser.add_argument(
        "--timeout", type=int, default=300, help="Per-problem timeout in seconds"
    )
    parser.add_argument("--config", type=Path, help="TOML config file")
    parser.add_argument(
        "--debug", action="store_true",
        help="Run scaffold in debug mode (enables verbose logging)",
    )
    parser.add_argument(
        "--scaffold-home", type=Path,
        default=Path.home() / ".local" / "scaffold",
        help="Scaffold home dir to sync OAuth credentials from",
    )
    args = parser.parse_args()

    config = load_config(args.config)
    scaffold_binary = (
        args.scaffold_binary
        if args.scaffold_binary is not None
        else config.scaffold_binary
    )
    model = args.model if args.model is not None else config.model
    workdir = Path(args.workdir)
    workdir.mkdir(parents=True, exist_ok=True)
    scaffold_home = (
        args.scaffold_home
        if any((args.scaffold_home / f).exists() for f in ("oauth2.db", "config.json"))
        else None
    )

    problems = load_problems(
        args.dataset,
        version=args.version,
        instance_ids=args.instance_ids,
        max_instances=args.max_instances,
    )

    if not problems:
        print("[livecodebench] No problems to run.", file=sys.stderr)
        sys.exit(1)

    print(
        f"[livecodebench] Running {len(problems)} problems with model={model}",
        flush=True,
    )

    output_path = Path(args.output)
    if output_path.exists():
        print(
            f"[livecodebench] Warning: {output_path} exists, overwriting.",
            file=sys.stderr,
        )
        output_path.write_text("")

    for problem in problems:
        try:
            prediction = run_problem(
                problem=problem,
                scaffold_binary=scaffold_binary,
                model=model,
                workdir=workdir,
                timeout=args.timeout,
                env_vars=config.env_vars,
                scaffold_home=scaffold_home,
                debug=args.debug,
            )
            with open(output_path, "a") as f:
                f.write(json.dumps(prediction) + "\n")
        except Exception:
            print(
                f"[livecodebench] ERROR on {problem.get('question_id', '?')}:",
                file=sys.stderr,
            )
            traceback.print_exc()

    print(f"[livecodebench] Done. Predictions written to {output_path}", flush=True)


if __name__ == "__main__":
    main()
