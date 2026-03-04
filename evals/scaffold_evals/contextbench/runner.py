"""Context-Bench evaluation runner for scaffold."""

from __future__ import annotations

import argparse
import json
import sys
import traceback
from pathlib import Path

from scaffold_evals.common.config import load_config
from scaffold_evals.common.scaffold_runner import extract_last_assistant_text, run_scaffold
from scaffold_evals.contextbench.prompt import build_prompt


def load_questions(questions_path: Path) -> list[dict]:
    """Load questions from a JSONL file.

    Each line should have: {"id": "...", "question": "...", "expected": "..."}
    """
    questions = []
    with open(questions_path) as f:
        for line in f:
            line = line.strip()
            if line:
                questions.append(json.loads(line))
    return questions


def run_question(
    question: dict,
    scaffold_binary: str,
    model: str,
    data_dir: Path,
    workdir: Path,
    timeout: int,
    env_vars: dict[str, str],
    scaffold_home: Path | None = None,
) -> dict:
    """Run scaffold on a single Context-Bench question."""
    qid = question["id"]
    qtext = question["question"]
    expected = question.get("expected", "")

    print(f"[contextbench] Running {qid}...", flush=True)

    system_prompt = build_prompt(qtext)
    home_dir = workdir / "homes" / str(qid)

    result = run_scaffold(
        message=qtext,
        working_dir=data_dir,
        system_prompt=system_prompt,
        scaffold_binary=scaffold_binary,
        model=model,
        home_dir=home_dir,
        scaffold_home=scaffold_home,
        timeout=timeout,
        env_vars=env_vars,
    )

    answer = extract_last_assistant_text(result.messages)

    print(f"[contextbench] {qid}: exit={result.exit_code}", flush=True)

    return {
        "id": qid,
        "question": qtext,
        "expected": expected,
        "answer": answer,
        "exit_code": result.exit_code,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description="Run Context-Bench evaluation with scaffold")
    parser.add_argument("--scaffold-binary", default=None, help="Path to scaffold binary")
    parser.add_argument("--model", default=None, help="Model to evaluate")
    parser.add_argument("--output", default="contextbench_results.jsonl", help="Output JSONL")
    parser.add_argument("--data-dir", required=True, type=Path, help="Directory with files to query")
    parser.add_argument("--questions", required=True, type=Path, help="JSONL with questions")
    parser.add_argument("--workdir", default="/tmp/eval/contextbench", help="Working directory")
    parser.add_argument("--timeout", type=int, default=300, help="Per-question timeout in seconds")
    parser.add_argument("--config", type=Path, help="TOML config file")
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

    questions = load_questions(args.questions)
    if not questions:
        print("[contextbench] No questions to run.", file=sys.stderr)
        sys.exit(1)

    print(f"[contextbench] Running {len(questions)} questions with model={model}", flush=True)

    output_path = Path(args.output)
    if output_path.exists():
        print(f"[contextbench] Warning: {output_path} exists, overwriting.", file=sys.stderr)
        output_path.write_text("")

    for question in questions:
        try:
            result = run_question(
                question=question,
                scaffold_binary=scaffold_binary,
                model=model,
                data_dir=args.data_dir,
                workdir=workdir,
                timeout=args.timeout,
                env_vars=config.env_vars,
                scaffold_home=scaffold_home,
            )
            with open(output_path, "a") as f:
                f.write(json.dumps(result) + "\n")
        except Exception:
            print(
                f"[contextbench] ERROR on {question.get('id', '?')}:",
                file=sys.stderr,
            )
            traceback.print_exc()

    print(f"[contextbench] Done. Results written to {output_path}", flush=True)


if __name__ == "__main__":
    main()
