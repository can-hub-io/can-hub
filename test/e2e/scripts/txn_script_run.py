"""Runs a transaction script through python-can-hub and prints the outcome
the way can-hub-client txn does.

usage: txn_script_run.py <url> <channel> <script> [--state-dir D] [--cancel-after-steps N]

Prints "done steps=N" (exit 0), "aborted reason=R step=S missing=[...]"
(exit 2) or the script error (exit 1). --cancel-after-steps cancels from the
progress callback once N steps are completed.
"""

import argparse
import sys

import can

from canhub import ScriptError, TransactionAborted

EXIT_SCRIPT_ERROR = 1
EXIT_ABORTED = 2


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("url")
    parser.add_argument("channel")
    parser.add_argument("script")
    parser.add_argument("--state-dir")
    parser.add_argument("--cancel-after-steps", type=int)
    arguments = parser.parse_args()

    bus = can.Bus(interface="canhub", channel=arguments.channel, url=arguments.url, state_dir=arguments.state_dir, reliable=True)
    try:
        result = bus.run_script(arguments.script, progress=cancel_after(arguments.cancel_after_steps))
    except ScriptError as error:
        print(f"line={error.line} {error}", file=sys.stderr, flush=True)
        sys.exit(EXIT_SCRIPT_ERROR)
    except TransactionAborted as aborted:
        report = aborted.result
        print(f"aborted reason={report.reason} step={report.failing_step} missing={sorted(report.missing)}", flush=True)
        sys.exit(EXIT_ABORTED)
    finally:
        bus.shutdown()

    print(f"done steps={result.completed_steps}", flush=True)


def cancel_after(steps):
    if steps is None:
        return None
    return lambda progress, total_steps: progress.completed_steps < steps


main()
