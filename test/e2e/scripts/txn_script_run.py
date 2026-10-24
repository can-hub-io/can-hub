"""Runs a transaction script through python-can-hub and prints the outcome
the way can-hub-client txn does.

usage: txn_script_run.py <url> <channel> <script> [--text] [--state-dir D] [--cancel-after-steps N]

Prints "roster step=S missing=[...]" for every step waiting for nodes that
moved on without all of them, then "done steps=N" (exit 0), "aborted
reason=R step=S missing=[...]" (exit 2) or the script error (exit 1).
--text reads the script and runs it as text, its files resolving against its
directory; --cancel-after-steps cancels from the progress callback once N
steps are completed.
"""

import argparse
import os
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
    parser.add_argument("--text", action="store_true")
    arguments = parser.parse_args()

    bus = can.Bus(interface="canhub", channel=arguments.channel, url=arguments.url, state_dir=arguments.state_dir, reliable=True)
    try:
        result = run(bus, arguments)
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


def run(bus, arguments):
    progress = cancel_after(arguments.cancel_after_steps)
    if not arguments.text:
        return bus.run_txn_script_file(arguments.script, progress=progress, on_roster=print_roster)
    with open(arguments.script) as script:
        text = script.read()
    return bus.run_txn_script(text, base_directory=os.path.dirname(arguments.script), progress=progress, on_roster=print_roster)


def print_roster(result):
    print(f"roster step={result.step} missing={sorted(result.missing)}", flush=True)


def cancel_after(steps):
    if steps is None:
        return None
    return lambda progress, total_steps: progress.completed_steps < steps


main()
