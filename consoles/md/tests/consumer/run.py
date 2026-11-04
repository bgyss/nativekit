#!/usr/bin/env python3
"""Configure, build, and run the standalone nmd consumer project."""
import os
import pathlib
import shlex
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[3]
compiler = os.environ.get("CC")
if not compiler and sys.platform == "darwin":
    compiler = "/usr/bin/clang"
compiler_words = shlex.split(compiler) if compiler else ["cc"]
if not compiler_words:
    raise SystemExit("consumer: CC must name a C compiler")
env = os.environ.copy()
env["CC"] = compiler or compiler_words[0]
build = ROOT / "build" / ("md-consumer-" + pathlib.Path(compiler_words[0]).name)

subprocess.run(["cmake", "-S", str(HERE), "-B", str(build),
                "-DNATIVEKIT_ROOT=" + str(ROOT)], check=True, env=env)
subprocess.run(["cmake", "--build", str(build), "-j8"], check=True, env=env)
executable = build / ("md_consumer.exe" if sys.platform == "win32" else "md_consumer")
subprocess.run([str(executable), "--headless", "--frames", "5"], check=True, env=env)
