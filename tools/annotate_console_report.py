#!/usr/bin/env python3
"""Attach tester-supplied console provenance without rewriting measurements."""
import argparse
import hashlib
import json
from pathlib import Path
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('report', type=Path)
p.add_argument('--firmware', required=True)
p.add_argument('--loader', required=True)
p.add_argument('--elf', type=Path, required=True)
p.add_argument('--run-id', required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
data = json.loads(a.report.read_text())
if data.get('schema_version') != 1 or data.get('backend') != 'ps5':
    p.error('requires a version 1 PS5 report; host evidence cannot be relabeled')
if a.output.resolve() == a.report.resolve():
    p.error('output must differ from the original report')
data['tester_annotations'] = {
    'firmware': a.firmware, 'loader': a.loader, 'run_id': a.run_id,
    'elf_sha256': hashlib.sha256(a.elf.read_bytes()).hexdigest(),
    'source_report_sha256': hashlib.sha256(a.report.read_bytes()).hexdigest(),
    'provenance': 'tester-supplied; original measurements preserved',
}
a.output.write_text(json.dumps(data, indent=2) + '\n')
