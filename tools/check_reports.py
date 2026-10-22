#!/usr/bin/env python3
"""Check CLI report contracts without promoting host results to console evidence."""
import json
from pathlib import Path
import sys
report, apk = [json.loads(Path(path).read_text()) for path in sys.argv[1:]]
assert report['schema_version'] == 1
assert report['backend'] == 'posix', report['backend']
assert report['android_runtime'] == 'blocked'
assert report['platform_probes_passed'] and all(p['passed'] for p in report['probes'])
assert report['firmware'] == report['loader'] == 'unrecorded'
assert report['revision']
assert apk['is_apk'] and apk['package_name'] == 'org.ps5droid.inspection'
assert apk['min_sdk'] == 28 and apk['target_sdk'] == 35
