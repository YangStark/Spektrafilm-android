# RAW memory admission proposal

Related to [#204](https://github.com/thetechgeekko/Spektrafilm-android/issues/204).
This is a proposed decoder policy, not a complete export-memory admission
solution and not a claim that the issue is resolved.

The decoder takes one Android total/available-memory snapshot per request and
applies its selected pixel ceiling at every geometry check. See
[RAW_DNG.md](../RAW_DNG.md) for the 8/12/24/32 MiPixel tiers. Missing or
inconsistent telemetry retains the baseline. Encoded-input and raw-store caps
remain 64 MiB and 128 MiB. Half-size requests do not bypass full-geometry checks:
some layouts can ignore that option.

## Validation on 2026-10-16

- Expanded resource tests cover exact memory thresholds and one byte below,
  both total and available requirements, 32-bit behavior, missing/partial and
  inconsistent telemetry. Existing production-wrapper hostile-input tests remain.
- Full LibRaw host CTest: **7/7 passed** with GCC 12.2, Debug, sanitizers OFF
  and OpenMP OFF, using the verified project-patched LibRaw 0.22.2 source.
- ASan/UBSan build succeeded. Two full suite runs passed 6/7 and 5/7;
  different tests intermittently timed out with `AddressSanitizer:DEADLYSIGNAL`.
  The resource-limit test passed in the first run and the allocation registry
  also passed when directly rerun. There is **no clean complete sanitizer run**;
  the cause remains unresolved and should be checked in the supported CI host.
- Shared unchanged app/JVM sources: 643 unit tests passed and lint passed on
  the separately prepared GPU worktree at the same upstream base.

Build instructions are in
[the host test README](../../lib/libraw/src/test/host/README.md).
Do not replace the sanitizer gate with the non-sanitized result above.

## Device evidence and limits

The earlier combined Release APK (`d6bf4a2`, based on v0.10.0) exported twenty
distinct Nikon Zf NEFs at 6064 x 4040 or portrait orientation on a 12 GB Galaxy
S24+ / Android 16. The 18-file batch sampled peak PSS at about 4.41 GiB. This is
evidence for that device and build; the separated candidate has not received
a fresh end-to-end APK qualification. No 33 MP file has been device-qualified.

A DPReview GFX 100 II RAF of 108,226,912 bytes was also tried in that installed
APK: preview was refused with `RAW input exceeds 64 MiB safety limit` and the
app remained responsive. It never reached pixel admission or half-size decode.
Supporting larger encoded files requires its own bounded-input work.

Low-memory devices retain the baseline, so this does not claim to fix the
low-memory report in #260. Maintainer agreement on the policy and a clean
sanitizer run are still needed before treating it as qualified for merging.
