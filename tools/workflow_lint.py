#!/usr/bin/env python3
"""Checks the workflow files before GitHub ever sees them.

    python3 tools/workflow_lint.py [.github/workflows] [--no-yaml]

Two things went wrong in this repository that this would have caught, both in
the release workflow and both only discovered by pushing a tag:

  * an `else` branch was turned into `elif ... else ... fi` while the original
    `fi` stayed behind, so the whole build step was a shell syntax error;
  * a step ran `make STATIC=1` for every platform, which the Apple linker
    cannot do.

This checks what can be checked without a runner: every `run:` block is valid
POSIX shell after the `${{ ... }}` expressions are substituted, every
`make <target>` a step calls exists in the Makefile, and - when PyYAML happens
to be installed - the file parses as YAML and every job has a runner.

The `run:` blocks are found by indentation rather than by parsing the YAML,
because the macOS runners' python3 has no PyYAML and the check has to work
everywhere.  `--no-yaml` forces that path, which is also how the fallback gets
tested.  This cannot tell whether the build will work - only whether the file
is broken.
"""
import os
import re
import subprocess
import sys

try:
    import yaml
except ImportError:  # the macOS runner's python3 has no python3-yaml
    yaml = None


def make_targets(root):
    targets = set()
    path = os.path.join(root, "Makefile")
    if not os.path.exists(path):
        return targets
    with open(path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            m = re.match(r"^([A-Za-z0-9_.-]+):(?!=)", line)
            if m:
                targets.add(m.group(1))
    return targets


def indentation(line):
    return len(line) - len(line.lstrip())


def run_blocks(text):
    """Yield (line number, script) for every `run:` in a workflow.

    Handles both the block form (`run: |`) and the one-liner (`run: make -j`),
    and stops a block at the first line that is indented no deeper than the
    `run:` key itself - which is exactly where the next YAML key starts.
    """
    lines = text.splitlines()
    i = 0
    while i < len(lines):
        m = re.match(r"^(\s*)(?:-\s+)?run:\s*(.*)$", lines[i])
        if not m:
            i += 1
            continue
        key_indent = indentation(lines[i])
        rest = m.group(2).strip()
        if rest and not rest.startswith(("|", ">")):
            yield i + 1, rest
            i += 1
            continue
        body = []
        j = i + 1
        while j < len(lines):
            nxt = lines[j]
            if nxt.strip() and indentation(nxt) <= key_indent:
                break
            body.append(nxt)
            j += 1
        strip = min((indentation(b) for b in body if b.strip()), default=0)
        yield i + 1, "\n".join(b[strip:] if b.strip() else "" for b in body)
        i = j


def check_script(label, script, targets, problems):
    probe = re.sub(r"\$\{\{[^}]*\}\}", "EXPR", script)
    r = subprocess.run(["sh", "-n"], input=probe, text=True, capture_output=True)
    if r.returncode != 0:
        problems.append("%s: shell syntax: %s" % (label, r.stderr.strip()))
    # `make` the command, not tools/make_big_docs.py
    for cmd in re.findall(r"(?<![\w/.-])make(?![\w.-])[^\n]*", script):
        # cut the command at the first command substitution: what is inside
        # $(...) is not a make argument
        cmd = re.sub(r"\$\([^)]*\)", "", cmd)
        for tok in cmd.split()[1:]:
            # stop at redirections, quotes and shell punctuation, and skip
            # variable assignments (STATIC=1, OPT=...) and options
            if tok[0] in "\"'|;&)#":
                break
            if "=" in tok or tok.startswith("-") or tok.endswith("\\") or "$" in tok:
                continue
            if tok.startswith("2>") or tok.startswith(">"):
                break
            if targets and tok not in targets:
                problems.append("%s: 'make %s' - no such target in the Makefile" % (label, tok))
            break


def check_structure(name, text, problems):
    """The YAML-only checks: it parses, it has jobs, each job has a runner."""
    if yaml is None:
        if "runs-on:" not in text:
            problems.append("%s: no runs-on anywhere - is this a workflow?" % name)
        return
    try:
        doc = yaml.safe_load(text)
    except yaml.YAMLError as e:
        problems.append("%s: not valid YAML: %s" % (name, e))
        return
    if not isinstance(doc, dict) or "jobs" not in doc:
        problems.append("%s: no jobs" % name)
        return
    if "on" not in doc and True not in doc:
        problems.append("%s: no trigger ('on:')" % name)
    for job_name, job in doc["jobs"].items():
        if not isinstance(job, dict):
            problems.append("%s: job %s is not a mapping" % (name, job_name))
            continue
        if "runs-on" not in job:
            problems.append("%s: job %s has no runs-on" % (name, job_name))


def main():
    argv = [a for a in sys.argv[1:] if a != "--no-yaml"]
    use_yaml = yaml is not None and "--no-yaml" not in sys.argv
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    wf_dir = argv[0] if argv else os.path.join(root, ".github/workflows")
    targets = make_targets(root)
    problems = []
    checked = 0
    for name in sorted(os.listdir(wf_dir)):
        if not name.endswith((".yml", ".yaml")):
            continue
        text = open(os.path.join(wf_dir, name), encoding="utf-8").read()
        for lineno, script in run_blocks(text):
            checked += 1
            check_script("%s: line %d" % (name, lineno), script, targets, problems)
        if use_yaml:
            check_structure(name, text, problems)
        elif "runs-on:" not in text:
            problems.append("%s: no runs-on anywhere - is this a workflow?" % name)

    for p in problems:
        print("error: " + p)
    note = "" if use_yaml else "  (no PyYAML: the YAML structure checks were skipped)"
    print("%d run blocks checked in %s%s" % (checked, os.path.relpath(wf_dir, root), note))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
