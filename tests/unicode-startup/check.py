"""Exercise real startup with Unicode arguments, environment and redirected I/O."""
import os
import subprocess
import sys

text = "acción_日本_😀"
env = dict(os.environ, KTH_UNICODE_STARTUP=text)
def run(argument):
    result = subprocess.run([sys.argv[1], argument], input=b"", capture_output=True, env=env)
    if result.returncode:
        raise RuntimeError(f"Startup {argument!r} failed ({result.returncode}): "
                           f"stdout={result.stdout!r}; stderr={result.stderr!r}")
    return result

result = run(text)
if result.stdout != text.encode("utf-8"):
    raise RuntimeError(f"Unexpected startup output: {result.stdout!r}")

if os.name == "nt":
    run("--closed-stdout")
