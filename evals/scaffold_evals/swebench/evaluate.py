"""Thin wrapper around swebench.harness.run_evaluation."""

from __future__ import annotations

import argparse
import subprocess
import sys


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Run SWE-bench evaluation harness on predictions"
    )
    parser.add_argument("--predictions", required=True, help="Path to predictions JSONL")
    parser.add_argument(
        "--dataset",
        default="princeton-nlp/SWE-bench_Verified",
        help="HuggingFace dataset name",
    )
    parser.add_argument("--split", default="test", help="Dataset split")
    parser.add_argument("--max-workers", type=int, default=4, help="Parallel evaluation workers")
    parser.add_argument("--run-id", default="scaffold", help="Run identifier")
    parser.add_argument(
        "--cache-level",
        default="env",
        choices=["none", "base", "env", "instance"],
        help="Docker caching level",
    )
    args = parser.parse_args()

    cmd = [
        sys.executable,
        "-m",
        "swebench.harness.run_evaluation",
        "--predictions_path", args.predictions,
        "--dataset_name", args.dataset,
        "--split", args.split,
        "--max_workers", str(args.max_workers),
        "--run_id", args.run_id,
        "--cache_level", args.cache_level,
    ]

    print(f"[swebench-eval] Running: {' '.join(cmd)}", flush=True)
    result = subprocess.run(cmd)
    sys.exit(result.returncode)


if __name__ == "__main__":
    main()
