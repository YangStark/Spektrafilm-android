# Large-frame Vulkan processing

This is a contribution validation note, not a release qualification or a change
to the application's RAW admission policy.

## Problem and scope

A 6064 x 4040 RGB float frame occupies 293,982,720 bytes. On the tested Adreno
750, `maxStorageBufferRange` is 128 MiB. Passing the whole frame through one
storage descriptor therefore rejects GPU halation, DIR and grain even when
the device has enough memory for the operation.

The patch keeps grain input/output slices within 32 MiB, with complete rows,
the blur halo and global random indices. Halation/DIR retain their full-line
recursive filters and transpose: storage descriptors expose bounded windows
of the full buffers, without introducing filter tile boundaries. Large spatial
scratch buffers are released after synchronized readback so the next native
allocation does not compete with a retained four-frame working set.

The original spatial shader handles frames that fit the device's single-buffer
storage range; the larger layout is selected only when that range is exceeded.
This preserves the existing path on devices with fewer descriptors. Unsupported capabilities, allocation
failure and unsupported numeric parameters retain CPU fallback.

## Prior integrated device evidence

These measurements belong to the combined local test build at `d6bf4a2`, based
on v0.10.0. That build also raised RAW admission on capable devices; this GPU-only
contribution does not. They establish the motivating end-to-end result, not
that every later revision has been APK-qualified.

- Release build, Galaxy S24+ SM-S926U1, Snapdragon 8 Gen 3 / Adreno 750,
  Android 16, nominal 12 GB RAM; ordinary indoor conditions, no extra cooling,
  unknown room temperature, USB connected.
- Twenty distinct Nikon Zf NEFs exported at full resolution, 6064 x 4040 or
  its portrait orientation. Halation, DIR and grain reported GPU engagement.
- The consecutive 18-file JPEG batch used one application process. Export
  times were 14.635-15.572 s (median 14.8835 s). This was an interactive test
  with gaps, not a controlled thermal benchmark or an endurance qualification.
- Sampled peak PSS during that batch was 4,628,364 KiB (about 4.41 GiB).
  Sampling can miss instantaneous peaks. This remains a memory-intensive path.
- A separate 6064 x 4040 synthetic GPU gate, with colored impulses spanning
  both descriptor boundaries, measured max absolute error `6.186e-7` and RMS
  `1.113e-8` against the CPU spatial reference. GPU device accounting peaked at
  1,175,930,880 bytes and returned to zero after the operation.
- The grain slice-size regression compared output bytes and diagnostic
  counters for multiple slice sizes and blur radii. This checks slicing
  invariance; upstream GPU grain is statistically equivalent to CPU grain,
  not the same per-pixel random realization.

Personal NEFs and their EXIF-bearing exports are intentionally not test
fixtures. The committed large-frame regression generates its own input.

## Validation boundaries

The prepared contribution was separately rebuilt with NDK r28c, arm64-v8a,
`CMAKE_BUILD_TYPE=Release` and Vulkan enabled. On the same Galaxy S24+, the
halation and grain device gates passed with `engaged=1`. The new large-frame
gate also passed, including small -> large -> small processing through one
context. It reproduced max absolute error `6.186e-7`, RMS `1.113e-8`, and zero
retained GPU device bytes immediately after the large operation.

Reproduce on Windows with an attached arm64 Vulkan device:

```powershell
./tools/gpu_probe/run_large_frame_gates.ps1 `
  -NdkRoot <sdk>/ndk/28.2.13676358 `
  -CMakeBin <sdk>/cmake/3.22.1/bin -Serial <adb-serial> -LargeFrame
```

`-Adb` accepts an explicit adb executable path. Without `-LargeFrame`, the
script runs the existing halation and grain gates, including the new grain
slicing regression. It builds the production CMake source list and requires
GPU engagement, without installing an APK. The small gates remain in the
existing lavapipe CI job; the large gate is opt-in because of its memory demand.

CPU host parity does not execute Vulkan; actual device engagement is required
for the GPU gates. The opt-in large-frame gate requires roughly 3 GiB of
process memory and must fail if the device is unavailable or falls back.

This does not establish support for all 33 MP files, all presets, every NEF
compression mode, low-memory devices or other GPU vendors. In particular,
raising a decoder ceiling and guaranteeing a complete export are separate
problems. This patch does not resolve the broader export admission decision
in [#204](https://github.com/thetechgeekko/Spektrafilm-android/issues/204).
