# Google Pixel Watch 3 ("helios") — GPL Kernel Source, 6.6.118

This repository contains the real, unmodified-content GPL kernel source for the **Google Pixel Watch 3** (codename **helios**, part of the **solios** device family), obtained directly from Google under the GPLv2 source-request process.

## Exact device / build match


| Device | Google Pixel Watch 3, 41mm, GA05757-US |
| Codename | `helios` (device family: `solios` / `seluna`) |
| SoC | Qualcomm SW5100 (Snapdragon W5 Gen 1) |
| Android build | `CP2A.260603.001.S1` |
| Real running kernel version string | `6.6.118-android15-8-ge6d21220be73-ab15042318-4k` |
| Kernel version (confirmed from `Makefile`) | `VERSION=6 PATCHLEVEL=6 SUBLEVEL=118` → **6.6.118** |

This repo intentionally contains **only the actual kernel source**, not the build toolchain or build tooling vendored dependencies:

- `common/` — the real, unmodified Linux/GKI kernel source tree (confirmed `6.6.118`).
- `private/` — the SoC-specific vendor source tree (Qualcomm SW5100 / "monaco") and the `helios`/`seluna` device tree, including overlays for every known hardware revision (DVT, EVT, EVT1.5, NFF 2.0/2.1, proto).

**Not included in this repo** (available instead as a [Release](../../releases) asset, see below):
- The Bazel/kleaf build tooling and vendored external dependencies (bazel-skylib, rules_python, etc.) — these are their own independent open-source projects.
- The exact hermetic Clang toolchain this kernel is built with (see below) — it is Apache-2.0/LLVM-licensed, not GPL, and is not kernel source.

This kernel is built with a **specific, pinned Clang revision**: `clang-r510928` (confirmed via `CLANG_VERSION=r510928` in `common/build.config.constants

As of this writing, `clang-r510928` (dated ~Jan 2024) has already been pruned from the tip of Google's public Clang prebuilts mirror (`android.googlesource.com/platform/prebuilts/clang/host/linux-x86`), which only keeps the last handful of revisions checked out at `main`. Rather than sending people on a fragile hunt through that repo's history, **the exact toolchain we used is attached to this repository's [Releases](../../releases) page**, bundled together with a full copy of the original, complete, self-contained workspace tarball Google sent us (source + this exact Clang + all vendored Bazel/kleaf dependencies) — in case you'd rather start from the pristine, 100%-as-received source instead of this repo's trimmed-down version.


```bash
# From this repo's root (after also fetching the Clang toolchain from Releases
# and placing it at external/kleaf/prebuilts/clang/host/linux-x86/clang-r510928/,
# or by starting from the full original tarball in Releases instead):
tools/bazel build --config=seluna --config=stamp --config=fast \
//private/devices/google/seluna:sw5100_seluna_dist

# To materialize the real output artifacts (boot.img, dtbo.img, vendor_dlkm.img,
# vendor_kernel_boot.img, kernel modules, etc.) into a destination directory:
tools/bazel run --config=seluna --config=stamp --config=fast \
//private/devices/google/seluna:sw5100_seluna_dist -- --destdir=/path/to/output
```

### Known gotcha: root `.bazelrc` must import `flags.bazelrc` before `common.bazelrc`

If you're building from the **original tarball** (Releases) rather than this repo, you'll need to create a root `.bazelrc` yourself — Google's drop doesn't ship one. A naive `.bazelrc` that's just a symlink to `external/kleaf/build/kernel/kleaf/common.bazelrc` will fail on the very first build attempt with:

```
ERROR: --allow_ddk_unsafe_headers :: Unrecognized option: --allow_ddk_unsafe_headers
```

This happens because Bazel reads the implicit workspace `.bazelrc` *before* the `--bazelrc=` files that `tools/bazel`'s own wrapper (`bazel.py`) injects on the command line — so by the time `device.bazelrc` tries to use the `--allow_ddk_unsafe_headers` flag alias, the file that registers that alias (`flags.bazelrc`) hasn't been loaded yet.

**Fix**: make the root `.bazelrc` a real (non-symlink) file that imports `flags.bazelrc` first:

```
try-import %workspace%/external/kleaf/build/kernel/kleaf/bazelrc/flags.bazelrc
try-import %workspace%/external/kleaf/build/kernel/kleaf/common.bazelrc
```

This repo's own `.bazelrc` already has this fix applied, the original kernel source from releases no
