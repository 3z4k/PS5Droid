#!/usr/bin/env python3
"""Validate repository Markdown links and capability documentation invariants."""
from pathlib import Path
import re
import sys
root = Path(__file__).resolve().parents[1]
errors = []
files = [root / 'README.md', root / 'THIRD_PARTY_NOTICES.md', *sorted((root / 'docs').glob('*.md'))]
for path in files:
    text = path.read_text()
    for target in re.findall(r'\]\(([^\s)]+)(?:\s+[^)]*)?\)', text):
        if '://' in target or target.startswith(('#', 'mailto:')):
            continue
        target = target.split('#', 1)[0]
        if target and not (path.parent / target).exists():
            errors.append(f'{path.relative_to(root)}: missing link {target}')
readme = (root / 'README.md').read_text()
for required in ['Android applications do not run yet', 'No verified console result', 'cooperative']:
    if required not in readme:
        errors.append(f'README is missing capability boundary: {required}')
if 'releases/latest' in (root / '.github/workflows/ci.yml').read_text():
    errors.append('CI must use an immutable SDK URL and checksum')
for error in errors:
    print(error, file=sys.stderr)
sys.exit(bool(errors))
