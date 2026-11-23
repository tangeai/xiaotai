# Unified distribution versions and BIN release rules

[中文](release-policy.md) · [English](release-policy.en.md)

Starting with this migration, one distribution version has one GitHub Release and one complete BIN per flashable board/variant. Do not upload firmware ZIPs. Put the version first in filenames, and publish checksums and flashing instructions in the release body.

GitHub-generated Source code archives are not firmware and are outside this asset rule.

The Release title must exactly match its tag, for example `v1.2.0-beta.1`. Put board counts, models, and bilingual descriptions in the body.

## Timeline and versions

These unified milestones follow source history without rewriting board version history. This maintainer-authorized migration removes legacy non-version tags after backing up tag objects, commit mappings, and a recovery bundle. Only the first three version tags below remain afterward.

Create annotated tags that state when and why a historical milestone was recorded; never backdate their creation to imply an original release date.

| Tag | Source date | Commit | Meaning |
|---|---|---|---|
| `v1.0.0` | 2026-11-06 | `01841cb8d0dee54e1d302e3b8ee92e66905a7903` | Five-board features and sound-recognition integration; retrospective source milestone |
| `v1.1.0` | 2026-11-11 | `0540ad0fe9c6166625220147ba8d0d822c5c1b77` | Unified five-board media buffering and four-viewer talkback; retrospective source milestone |
| `v1.2.0-beta.1` | 2026-11-21 | `1e9f0811f251c8d9fdab7b910ba2c74bc7596ec1` | PLC/FEC, SDK, serial recovery and lifecycle fixes |
| `v1.2.0-beta.2` | 2026-11-23 | `4dc2f93d278d0fcb39be9bc7f7bbfe651f000a55` | Five boards rebuilt with TiRTC 2.6.3, hardware acceptance pending |
| `v1.2.0-beta.3` | 2026-11-23 | `6b6a9d607f1f2bb181298ac32f068e011261bff6` | AI short-packet and concurrent admission fixes, shared maintenance console, hardware acceptance pending |
| `v1.2.0` | 2026-11-23 | `3db47969be8bd17bce5068a10fc4bbd65d51505f` | Stable distribution: AI talkback pause restoration and playback buffering changes, same source for five boards |

`v1.0.0` and `v1.1.0` are retrospective source tags with archives of actual historical complete BINs. Each board retains its original embedded version, build source, and hash; not all images were compiled from the unified tag.

M5 archives contain only ILI9342C; v1.1.0 reuses its older build.48. LCKFB S3 build.68 still has a known issue, so v1.1.0 remains pre-release. Original Release pages remain evidence; old asset URLs expired with their tags, and the download page links to the new archives.

Reproduce historical source using the commit SHA in those notes, not deleted board tags.

Keep version tags immutable; this one-time cleanup does not authorize routine tag deletion.

Use `1.2.1` for compatible fixes, `1.3.0` for compatible features, and `2.0.0` for breaking changes; build counts do not determine minor versions.

## Distribution and firmware identities

For the historical beta.1 release, `1.2.0-beta.1` was the unified distribution version, not a newly compiled embedded firmware version. Reused builds retain their actual identities; do not claim that devices report `1.2.0`.

| Board / variant | Reused embedded firmware identity |
|---|---|
| LCKFB BK7258 | `1.0.2+build.99` |
| MGKFB BK7258 | `1.0.3+build.49` |
| LCKFB ESP32-S3 | `1.1.0+build.82` |
| Waveshare ESP32-P4 | `1.1.0+build.48` |
| M5 ILI9342C | `1.1.0+build.91` |

Record distribution version, channel, board ID, variant, embedded version, build number, original build commit, integrated commit, and download BIN SHA-256 in release notes. When build and release commits differ, retain source-equivalence evidence and distinguish the identities.

Starting with beta.2, all five boards use firmware base version `1.2.0`. Board build counters remain independent and monotonic, including failures and retries.

## Downloads

Use `v<distribution-version>_<board-id>[_<variant>]_<image-type>.bin`, optionally omitting the suffix for a sole default variant. The release overview and download page must map each BIN link to the board’s Chinese and English names and the matching homepage photograph, without a separate display column.

The image type must be included: `merged` for ESP or `all-app` for BK.

The current five boards have five complete images. Retain both version and hardware identity to prevent confusion after download.

```text
v1.2.0-beta.1_lckfb-bk7258_all-app.bin
v1.2.0-beta.1_mgkfb-bk7258_all-app.bin
v1.2.0-beta.1_lckfb-esp32s3_merged.bin
v1.2.0-beta.1_waveshare-esp32p4-touch-lcd-43c-v10_merged.bin
v1.2.0-beta.1_m5stack-cores3-lite_ili9342c_merged.bin
```

ESP downloads are complete raw BINs merged using actual native `flasher_args.json` addresses, with base `0x0`. Verify every segment, address, total length, and `0xFF` padding. Include P4 OTA initialization data.

Full flashing overwrites NVS in gaps and requires Wi-Fi and binding setup again; configuration retention and OTA are not promised.

BK downloads are byte-identical copies of official `all-app.bin`, with only external filenames changed and SHA-256 checked. Use the board guide's BK serial tool and parameters, not esptool. A shared extension does not imply a shared format.

Prepare to reconfigure after full flashing; actual configuration effects depend on native partitions and flashing operations.

Maintainers retain split images, ELF files, manifests, checksums, and evidence without uploading them as default firmware assets. Continue internal validation with `tools/release_firmware.py --package <ZIP> --notes-file <notes>`; add `--merged <original-name-complete-BIN>` for ESP.

Then copy to public names and compare bytes without increasing build counters. Current tool tag/title output uses the legacy format; generate public names under this policy instead.

## Release gates and workflow

Branch from current `main`, use PR review, CI and Squash merge, and verify the integrated commit before release. Do not use `develop` or a permanent release integration branch. Protect both `refs/tags/v*` and historical `refs/tags/*-v*` against updates and deletion.

For one-time migration deletion, temporarily exclude only the exact old refs, then restore and verify protection. Verify actual GitHub settings; documentation alone does not enable protection.

Use the [bilingual template](../../.github/release_notes_template.md) and verify board-specific changes against an explicit baseline. Each section may list boards separately. Chinese and English versions must agree on identities, hashes, known issues, and validation scope.

Host checks and native builds with pending hardware testing permit only pre-release, never Latest; retain `BUILD_VERIFIED_HIL_PENDING`. Never substitute stale firmware for a failed variant; explain omissions.

After publication, check tag targets, pre-release state, asset set and names, and download every BIN to verify SHA-256. Update beta navigation only after remote verification, never labeling the beta as stable. Do not overwrite historical releases or assets.

`v1.0.0` is a historical stable archive, while `v1.1.0` remains a pre-release due to known issues and the current stable release is `v1.2.0`, and `v1.2.0-beta.*` stay pre-releases.

Hardware acceptance applies to exact BIN bytes. An accepted BIN may be promoted to stable by external renaming without changing its hash. Changing embedded versions or recompiling requires new validation. Include only accepted boards/variants in stable releases; never borrow another variant's results.

Publish fixes as `beta.2` or later without overwriting `beta.1`.
