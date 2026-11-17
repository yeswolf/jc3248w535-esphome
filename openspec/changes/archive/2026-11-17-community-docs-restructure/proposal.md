# Proposal

## Why

The repository is meant to be shared with the JC3248W535 community, but it currently
serves two independent firmware build paths, ships prebuilt native images, and keeps the
board documentation scattered across third-party mirrors. A newcomer cannot tell which
build to use, where the schematic or the factory image lives, or what the hardware can
actually do. Consolidating the firmware on ESPHome and archiving every known board
document in one place turns the repository into a usable reference instead of a local
working tree.

## What Changes

- Add `docs/` with the complete board documentation archive: schematic sheets, power
  schematic, specification, getting-started manual, GPIO pinout, IO distribution, panel
  slot diagrams, IC datasheets (NS4168, AX98357A, ESP32-S3, ESP32-S3-WROOM-1), factory
  firmware images, burn instructions and the stock SD-card font set. The complete
  upstream package (numbered vendor folders, demo sources, board definitions and the
  full burn tool) is mirrored under `docs/vendor/JC3248W535EN/`; only the bundled
  LVGL/Arduino libraries, prebuilt ESP-IDF archives and sample media are left in the
  upstream repository.
- **BREAKING** Remove the native ESP-IDF application build path: `main/`, the root
  `CMakeLists.txt`, `partitions.csv`, `sdkconfig.defaults`, `dependencies.lock` and the
  prebuilt `firmware/` images. ESPHome becomes the only supported firmware.
- Keep `components/jc3248_media/` as the ESPHome external component, and drop the
  native-only build metadata from it (`CMakeLists.txt`, `idf_component.yml`) once the
  ESPHome build is confirmed to compile without them.
- **BREAKING** Remove the bundled sample videos in `samples/` and the sample-identity
  checks in the browser: the per-file size/CRC32 verification, the red "bad copy"
  marker, the "wrong file" error overlay and the sample names in the format dialog.
  No sample media is published with the repository.
- Move the factory images from `stock_firmware/` to `docs/factory-firmware/` and merge
  the two READMEs into one restore guide with burned-image instructions.
- Rewrite the root `README.md` as a rubricator: project summary, feature list, hardware
  table, quick start, repository map, documentation index, limitations, credits.
- Add `docs/README.md` and a README for each documentation section, recording the origin
  of every file.
- Document that the project currently has no LICENSE and that usage conditions are not
  yet defined.

## Capabilities

### New Capabilities

_None. This change is a documentation and repository-layout refactor; no firmware or
component behavior changes._

### Modified Capabilities

_None._

## Impact

- Repository layout and documentation only. The ESPHome configuration, the
  `jc3248_media` component sources and the device behavior stay unchanged.
- Removed artifacts: native build files and prebuilt native firmware images.
- Added artifacts: approximately 47 MB of board documentation and factory firmware.
- No changes to APIs, entities, partitions or runtime behavior.
