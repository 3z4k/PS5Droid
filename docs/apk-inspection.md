# Read-only Android package inspection

`--inspect-apk FILE` emits JSON and exits nonzero when inspection fails. Successful
output describes the real manifest package name, minimum/target SDK, non-directory
entry count, DEX entry names and native-library directory ABIs. These are metadata
observations; they do not assert executable validity or runtime compatibility.

## Parsing contract

The parser reads the end-of-central-directory record, validates the advertised
entry count and directory span, checks unique entry names and matching local
headers, and rejects overlapping payloads. Only the manifest is decompressed;
large APK assets are not loaded into memory. Its CRC and decompressed length are
checked before Android binary XML is parsed.

AXML parsing handles UTF-8 and UTF-16 string pools, typed attributes, the manifest
root and balanced element names. Package and SDK values come from manifest
attributes, not raw filename substrings or fabricated defaults. Android's absent
`uses-sdk` defaults are minimum SDK 1 and target SDK equal to the minimum.

| Limit | Value / behavior |
| --- | --- |
| Container | Single-disk ordinary ZIP; ZIP64 and encrypted entries rejected |
| Compression | Stored and DEFLATE entries |
| Central directory | At most 16 MiB; ordinary ZIP entry counts |
| Manifest | At most 4 MiB compressed and uncompressed |
| XML | Binary Android XML; string pools capped at 65,535 strings; bounded nesting |
| SDK attributes | Integer values; resource references and preview codenames rejected explicitly |
| Signatures | APK signing blocks may be present; signature verification is not implemented |
| Other entries | Names/structure inspected; DEX/native contents and their CRCs are not verified |
| Extraction/install | Not implemented |

## Independent fixture

`tests/fixtures/AndroidManifest.xml` is project-authored. Its binary fixture was
created with Google AAPT2 `8.8.2-12006047` and AOSP's API 35 public `android.jar`:

```sh
aapt2 link -I android-35.jar --manifest tests/fixtures/AndroidManifest.xml \
  --min-sdk-version 28 --target-sdk-version 35 --no-compile-sdk-metadata \
  -o tests/fixtures/aapt2-manifest.apk
aapt2 dump badging tests/fixtures/aapt2-manifest.apk
```

Expected package: `org.ps5droid.inspection`; minimum SDK **28**; target SDK **35**.
The fixture contains no Android framework code or native executable. Python builds
stored/deflated and malformed variants from it for reproducible host tests.

For another independent check, compare `apkanalyzer manifest application-id`,
`manifest min-sdk`, `manifest target-sdk` and `files list` with the CLI output.

Primary format/tool references: [AOSP ResourceTypes.h](https://android.googlesource.com/platform/frameworks/base/+/HEAD/libs/androidfw/include/androidfw/ResourceTypes.h),
[PKWARE APPNOTE](https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT),
[APK Analyzer](https://developer.android.com/tools/apkanalyzer).
