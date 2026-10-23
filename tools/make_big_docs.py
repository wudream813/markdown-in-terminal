#!/usr/bin/env python3
"""Writes the big documents the README's "Big documents" numbers come from.

    python3 tools/make_big_docs.py /tmp/big
    python3 tools/big_bench.py ./build/mdt /tmp/big/mixed10.md

mixed10.md (11.4 MB): the demo plus every test fixture, 783 times (remote
pictures removed, the numbers must not depend on the network); m40.md: the
same 40 times; prose10.md (11.7 MB): prose, lists, quotes, tables and code.
Nothing big is committed.
"""
import glob, os, random, re, sys
out = sys.argv[1] if len(sys.argv) > 1 else "/tmp/big"
os.makedirs(out, exist_ok=True)
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
parts = []
for f in [root + "/demo/demo.md"] + sorted(glob.glob(root + "/tests/fixtures/*.md")):
    s = open(f, encoding="utf-8", errors="replace").read()
    s = re.sub(r"!\[[^\]]*\]\(https?://[^)]*\)", "", s)
    s = re.sub(r"<img[^>]*https?://[^>]*>", "", s)
    parts.append(s)
unit = "\n\n".join(parts)
def mixed(n, name):
    with open(os.path.join(out, name), "w", encoding="utf-8") as f:
        for i in range(n):
            f.write(f"\n\n# Section {i}\n\n" + unit)
mixed(783, "mixed10.md")
mixed(40, "m40.md")
random.seed(7)
words = ("reader terminal markdown paragraph render width colour heading quote list table memory "
         "layout span block widget buffer cache stream symbol index 中文 段落 表格").split()
w = lambda n: " ".join(random.choice(words) for _ in range(n))
with open(os.path.join(out, "prose10.md"), "w", encoding="utf-8") as f:
    size = 0; n = 0
    while size < 11_700_000:
        n += 1
        chunk = (f"## Section {n}\n\n{w(120)} **{w(3)}** `{w(1)}` [{w(2)}](https://example.com/{n})\n\n"
                 f"- {w(14)}\n- {w(14)}\n\n> {w(30)}\n\n| a | b | c |\n|---|---|---|\n| {w(3)} | {w(2)} | {n} |\n\n"
                 f"```cpp\nint f{n}() {{ return {n}; }}\n```\n\n")
        f.write(chunk); size += len(chunk.encode())
for n in os.listdir(out): print(n, os.path.getsize(os.path.join(out, n)))
