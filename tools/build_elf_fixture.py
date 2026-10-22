#!/usr/bin/env python3
"""Build the trusted cooperative fixture using upstream LLVM's Android target."""
import argparse
from pathlib import Path
import subprocess
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--clang', default='clang')
p.add_argument('--linker', default='ld.lld')
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
a.output.parent.mkdir(parents=True, exist_ok=True)
obj = a.output.with_suffix('.o')
subprocess.run([a.clang, '--target=x86_64-linux-android28', '-ffreestanding', '-fPIC',
                '-fno-stack-protector', '-fno-unwind-tables', '-fno-asynchronous-unwind-tables',
                '-O2', '-c', str(root / 'tests/fixtures/cooperative_entry.c'), '-o', str(obj)], check=True)
subprocess.run([a.linker, '-shared', '--build-id=none', '-z', 'max-page-size=16384',
                '-T', str(root / 'tests/fixtures/cooperative_entry.ld'), str(obj), '-o', str(a.output)], check=True)
