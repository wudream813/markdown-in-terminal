#!/usr/bin/env python3
"""Checks the workflow files before GitHub ever sees them.

    python3 tools/workflow_lint.py [.github/workflows]

Two things went wrong in this repository that this would have caught, both in
the release workflow and both only discovered by pushing a tag:

  * an `else` branch was turned into `elif ... else ... fi` while the original
    `fi` stayed behind, so the whole build step was a shell syntax error;
  * a step ran `make STATIC=1` for every platform, which the Apple linker
    cannot do.

This checks what can be checked without a runner: the YAML parses, every `run:`
block is valid POSIX shell after the `${{ ... }}` expressions are substituted,
each job has a name and a runner, and every `make <target>` a step calls exists
in the Makefile.  It cannot tell whether the build will work - only whether the
file is broken.
"""
import os
import re
import subprocess
import sys

import yaml


def make_targets(root):
    targets = set()
    path = os.path.join(root, "Makefile")
    if not os.path.exists(path):
        return targets
    for line in open(path, encoding="utf-8", errors="replace"):
        m = re.match(r"^([A-Za-z0-9_.-]+):(?!=)", line)
        if m:
            targets.add(m.group(1))
    return targets


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    wf_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, ".github/workflows")
    targets = make_targets(root)
    problems = []
    checked = 0
    for name in sorted(os.listdir(wf_dir)):
        if not name.endswith((".yml", ".yaml")):
            continue
        path = os.path.join(wf_dir, name)
        try:
            doc = yaml.safe_load(open(path, encoding="utf-8"))
        except yaml.YAMLError as e:
            problems.append("%s: not valid YAML: %s" % (name, e))
            continue
        if not isinstance(doc, dict) or "jobs" not in doc:
            problems.append("%s: no jobs" % name)
            continue
        if "on" not in doc and True not in doc:
            problems.append("%s: no trigger ('on:')" % name)
        for job_name, job in doc["jobs"].items():
            if not isinstance(job, dict):
                problems.append("%s: job %s is not a mapping" % (name, job_name))
                continue
            if "runs-on" not in job:
                problems.append("%s: job %s has no runs-on" % (name, job_name))
            for step in job.get("steps", []):
                script = step.get("run")
                if not script:
                    continue
                checked += 1
                label = "%s: %s / %s" % (name, job_name, step.get("name", "run"))
                probe = re.sub(r"\$\{\{[^}]*\}\}", "EXPR", script)
                r = subprocess.run(["sh", "-n"], input=probe, text=True, capture_output=True)
                if r.returncode != 0:
                    problems.append("%s: shell syntax: %s" % (label, r.stderr.strip()))
                for cmd in re.findall(r"\bmake[^\n]*", script):
                    # cut the command at the first command substitution: what is
                    # inside $(...) is not a make argument
                    cmd = re.sub(r"\$\([^)]*\)", "", cmd)
                    for tok in cmd.split()[1:]:
                        # stop at redirections, quotes and shell punctuation, and
                        # skip variable assignments (STATIC=1, OPT=...) and options
                        if tok[0] in "\"'|;&)#":
                            break
                        if "=" in tok or tok.startswith("-") or tok.endswith("\\") or "$" in tok:
                            continue
                        if tok.startswith("2>") or tok.startswith(">"):
                            break
                        if targets and tok not in targets:
                            problems.append("%s: 'make %s' - no such target in the Makefile"
                                            % (label, tok))
                        break
    for p in problems:
        print("error: " + p)
    print("%d run blocks checked in %s" % (checked, os.path.relpath(wf_dir, root)))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
