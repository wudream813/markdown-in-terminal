#!/usr/bin/env python3
"""Two block images in one paragraph must stack, not land on the same row.

The img_only loop wrote every generated line with l.row = 0 because the row
offset was computed only once per paragraph (bl.row) and the loop variable
`row` was updated after the lines were emitted. One-paragraph-at-a-time docs
never noticed; two images in one paragraph drew on top of each other.

    python3 tools/multi_image_test.py ./build/mdt [fixture]
"""
import os
import re
import subprocess
import sys
import tempfile

BIN = sys.argv[1] if len(sys.argv) > 1 else "./build/mdt"
DOC = sys.argv[2] if len(sys.argv) > 2 else "tests/fixtures/multi-image.md"

env = dict(os.environ, MDT_DEBUG_SVG="1")
shot = os.path.join(tempfile.mkdtemp(prefix="mdt-mimg-"), "shot.png")
r = subprocess.run([BIN, f"--screenshot={shot}", DOC], env=env,
                   capture_output=True, text=True, timeout=60)
rows = [int(m.group(1)) for m in
        re.finditer(r"line image row=(\d+)", r.stderr + r.stdout)]
if len(rows) < 2:
    print(f"   FAIL expected at least 2 laid-out image rows, got {rows}")
    sys.exit(1)
# the first two block images of the doc sit in one paragraph
a, b = rows[0], rows[1]
if a == b:
    print(f"   FAIL both images of the same paragraph were placed on row {a} "
          "(they overlap)")
    sys.exit(1)
if b < a:
    print(f"   FAIL second image row {b} is above the first ({a})")
    sys.exit(1)
print(f"   ok block images in one paragraph stack on rows {a} and {b}")
