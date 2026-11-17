# Design

## Context

See `proposal.md` - Why. The current tree mixes two firmware entry points: the native
ESP-IDF application (`main/`, root `CMakeLists.txt`, `sdkconfig.defaults`,
`partitions.csv`, `dependencies.lock`, prebuilt `firmware/`) and the ESPHome application
(`esphome/`), which compiles the same sources in `components/jc3248_media/` as an
external component. Board documentation exists only as external links inside the README,
and the factory images sit in a top-level `stock_firmware/` directory without a clear
relationship to the rest of the documentation.

The upstream mirror `NorthernMan54/JC3248W535EN` holds the vendor package, but the
repository is about 1 GB: most of it is bundled LVGL/Arduino libraries, prebuilt SDK
archives and demo media, none of which is board information.

## Goals / Non-Goals

**Goals:**

- One supported firmware path: ESPHome, with the same feature set as today.
- A self-contained, browsable documentation archive with recorded provenance.
- A root README that works as the entry point and index for the whole repository.
- Keep the Git-tracked size of the documentation archive to roughly 72 MB.

**Non-Goals:**

- Do not vendor upstream LVGL, Arduino libraries, SDK archives or demo MP3/MJPEG media.
- Do not change the ESPHome configuration, entities, pin map or partition layout.
- Do not choose or add a project license in this change.
- Do not rewrite the `jc3248_media` C/C++ sources.

## Decisions

### Documentation tree

```
docs/
  README.md                 archive index, provenance, licensing status
  board/
    README.md               board description, ports, pinout, audio/display notes
    schematics/             ESP32_schematic.jpg, Power_schematic.jpg
    specifications/         vendor spec and getting-started PDFs
    pinout/                 GPIO pinout, IO distribution, board photos
    mechanical/             panel slot DXF files, dimension drawing
  datasheets/               NS4168, AX98357A, ESP32-S3, ESP32-S3-WROOM-1
  factory-firmware/
    README.md               restore and burn instructions
    images/                 merged factory images
    burn-instructions/      screenshots from the vendor burn package
    fonts/                  vendor SD-card font set for the stock application
  vendor/JC3248W535EN/      complete upstream package as received, minus the
                            bundled libraries and demo media
```

Rationale: the archive is grouped by what a reader is looking for (board, IC data,
firmware), not by the vendor package's numbered directory names. File names are
preserved where they are referenced elsewhere; otherwise they are lower-cased with the
vendor case removed. The full upstream package remains one link away.

Alternative considered: mirror the vendor tree verbatim (`JC3248W535EN/2-Specification`,
`8-Burn operation`, ...). Rejected: it preserves multi-gigabyte third-party content,
duplicates the README's job, and makes the archive harder to navigate.

### Firmware consolidation

ESPHome is the only build path. Remove `main/`, the root `CMakeLists.txt`,
`partitions.csv`, `sdkconfig.defaults`, `dependencies.lock` and `firmware/`.
`components/jc3248_media/` stays because the ESPHome YAML loads it through
`external_components`; its `CMakeLists.txt` and `idf_component.yml` describe the removed
native build and are dropped after a compile check confirms ESPHome does not need them.

Alternative considered: keep the native build as a second documented path. Rejected by
the project owner: two paths double the maintenance and confuse newcomers.

### README structure

The root README is a landing page and rubricator: hero image, one-paragraph pitch,
feature list, hardware table, quick start, repository map, documentation index, file
preparation, limitations, credits. Long operational details stay in `esphome/README.md`
and in the section READMEs; the root links to them instead of duplicating them.

### Sample media

The repository no longer ships `samples/` or any sample-specific browser behaviour. The
five bundled episodes were the only reason for the size/CRC32 validation, the red bad-copy
marker and the "wrong file" overlay; with the media gone those paths are dead code and are
removed together with the sample names in the format confirmation dialog. Files on the
card are now always listed and played by their actual content, with no name-based special
cases.

### Factory firmware

`stock_firmware/` moves under `docs/factory-firmware/` because it is reference material
for restoring the board, not part of the maintained firmware. The two vendor images are
kept (the `EN-80M` image is the vendor-named restore image; the second is the vendor
burn-files image). Duplicate images from the upstream burn package are not copied twice.

### Licensing

The project has no LICENSE and this change does not add one. `README.md` states that
usage conditions for the project's own code are not defined. The archive records the
origin of every file; vendor documents and factory images remain the property of
Shenzhen Jingcai Intelligent Co., Ltd.

## Risks / Trade-offs

- Removing the native build can surprise existing users -> the component sources remain,
  the ESPHome path is fully documented, and Git history retains the removed files.
- Unused `CMakeLists.txt` / `idf_component.yml` removal may break the ESPHome compile if
  ESPHome touches them -> verify with `esphome config` and a compile before declaring the
  change done; restore the files if needed.
- Documentation links rot when files are renamed -> keep names close to the vendor
  originals, add a link check pass over every README, and record upstream URLs.
- The archive adds about 72 MB to the repository -> Git handles it, no file approaches
  GitHub's 100 MB per-file limit, and the demo libraries are excluded.

## Migration Plan

1. Create the `docs/` tree and download the curated files from the upstream mirror.
2. Verify checksums of factory images against the existing `stock_firmware/` copies and
   remove duplicates.
3. Remove the native build files and prebuilt images; keep the component sources.
4. Rewrite the root README and add the section READMEs.
5. Validate every internal link, run `openspec validate`, and confirm the ESPHome
   configuration still resolves.

Rollback: restore the removed paths from Git (`git checkout -- <path>`); the change is
not committed until the owner asks.

## Open Questions

None.
