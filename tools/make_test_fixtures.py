#!/usr/bin/env python3
"""Create reproducible ZIP/AXML edge cases; no downloaded inputs are needed."""
import argparse
from pathlib import Path
import struct
import zipfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('output', type=Path)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
source = Path(__file__).resolve().parents[1] / 'tests/fixtures/aapt2-manifest.apk'
with zipfile.ZipFile(source) as z:
    manifest = z.read('AndroidManifest.xml')

def apk(name, items, compression=zipfile.ZIP_STORED):
    with zipfile.ZipFile(a.output / name, 'w', compression=compression) as z:
        for key, value in items:
            info = zipfile.ZipInfo(key, (2020, 1, 1, 0, 0, 0))
            info.compress_type = compression
            z.writestr(info, value)

files = [('AndroidManifest.xml', manifest), ('classes.dex', b'dex\n035\0'),
         ('classes2.dex', b'dex\n035\0'), ('lib/x86_64/libsample.so', b'fixture'),
         ('assets/message.txt', b'AndroidManifest.xml classes9.dex lib/other/libfake.so')]
apk('stored.apk', files)
apk('deflated.apk', files, zipfile.ZIP_DEFLATED)
apk('plain.zip', [('assets/message.txt', b'AndroidManifest.xml classes.dex')])
apk('bad-manifest.apk', [('AndroidManifest.xml', b'not binary XML')])
apk('duplicate.apk', [('AndroidManifest.xml', manifest), ('AndroidManifest.xml', manifest)])
apk('large-manifest.apk', [('AndroidManifest.xml', b'x' * (4 * 1024 * 1024 + 1))], zipfile.ZIP_DEFLATED)
# CRC corruption: mutate the stored manifest without altering its advertised CRC.
data = bytearray((a.output / 'stored.apk').read_bytes())
name_length, extra_length = struct.unpack_from('<HH', data, 26)
data[30 + name_length + extra_length + 10] ^= 0x10
(a.output / 'bad-crc.apk').write_bytes(data)
(a.output / 'truncated.apk').write_bytes((a.output / 'stored.apk').read_bytes()[:-10])
# A forged program header offset used by the deterministic malformed ELF corpus.
(a.output / 'stamp').write_text('ZIP fixtures generated\n')


def binary_xml(utf8=True):
    strings = ['manifest', 'package', 'org.ps5droid.encoding', 'uses-sdk',
               'minSdkVersion', 'targetSdkVersion',
               'http://schemas.android.com/apk/res/android', 'android']
    encoded = bytearray()
    offsets = []
    for text in strings:
        offsets.append(len(encoded))
        if utf8:
            raw = text.encode('utf-8')
            encoded += bytes([len(text), len(raw)]) + raw + b'\0'
        else:
            encoded += struct.pack('<H', len(text)) + text.encode('utf-16le') + b'\0\0'
    encoded += b'\0' * (-len(encoded) % 4)
    start = 28 + 4 * len(strings)
    pool = struct.pack('<HHIIIIII', 1, 28, start + len(encoded), len(strings), 0,
                       0x100 if utf8 else 0, start, 0)
    pool += struct.pack('<' + 'I' * len(offsets), *offsets) + encoded
    def node(kind, extension):
        return struct.pack('<HHIII', kind, 16, 16 + len(extension), 1, 0xffffffff) + extension
    def attr(ns, name, kind, value):
        return struct.pack('<IIIHBBI', ns, name, 0xffffffff, 8, 0, kind, value)
    def element(name, attrs):
        return node(0x102, struct.pack('<IIHHHHHH', 0xffffffff, name, 20, 20, len(attrs), 0, 0, 0)
                    + b''.join(attrs))
    namespace = struct.pack('<II', 7, 6)
    parts = [pool, node(0x100, namespace), element(0, [attr(0xffffffff, 1, 3, 2)]),
             element(3, [attr(6, 4, 0x10, 28), attr(6, 5, 0x10, 35)]),
             node(0x103, struct.pack('<II', 0xffffffff, 3)),
             node(0x103, struct.pack('<II', 0xffffffff, 0)), node(0x101, namespace)]
    body = b''.join(parts)
    return struct.pack('<HHI', 3, 8, 8 + len(body)) + body

for encoding in ['utf8', 'utf16']:
    xml = binary_xml(encoding == 'utf8')
    apk(f'{encoding}.apk', [('AndroidManifest.xml', xml)], zipfile.ZIP_DEFLATED)
xml = bytearray(binary_xml())
struct.pack_into('<I', xml, 12, 0)
apk('bad-chunk.apk', [('AndroidManifest.xml', xml)])
xml = bytearray(binary_xml())
struct.pack_into('<I', xml, 16, 0xffffffff)
apk('bad-pool.apk', [('AndroidManifest.xml', xml)])
xml = bytearray(binary_xml())
at = 8
while struct.unpack_from('<H', xml, at)[0] != 0x102:
    at += struct.unpack_from('<I', xml, at + 4)[0]
struct.pack_into('<H', xml, at + 26, 0)
apk('bad-attributes.apk', [('AndroidManifest.xml', xml)])
