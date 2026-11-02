#!/usr/bin/env python3
"""Check named and unnamed invalid-access diagnostics."""
import subprocess
import sys

for args, expected in [([], "an access of kind 2, not one of the console's 2"),
                       (["--names"], "an access of kind 2, not one of the console's 2 (the console's kinds: memory, io)")]:
    result = subprocess.run([sys.argv[1], *args], capture_output=True, text=True)
    if result.returncode != 70 or result.stderr.strip() != expected:
        sys.exit("kinds_check: unexpected result %s: %r" % (result.returncode, result.stderr))
print("kinds_check: ok")
