# Tasks

## 1. Documentation archive

- [x] 1.1 Create the `docs/` tree and download the vendor files from
  `NorthernMan54/JC3248W535EN` (schematics, specification, manual, pinout, DXF,
  datasheets, factory images, burn instructions and stock SD-card fonts).
- [x] 1.2 Verify factory image checksums against the existing `stock_firmware/` copies
  and keep one copy of each unique image.
- [x] 1.3 Write `docs/README.md` and per-section READMEs with provenance.
- [x] 1.4 Mirror the complete upstream vendor package under
  `docs/vendor/JC3248W535EN/`, excluding only the bundled libraries and demo media.

## 2. Firmware consolidation

- [x] 2.1 Remove the native ESP-IDF build files and prebuilt `firmware/` images.
- [x] 2.2 Remove the native-only build metadata from `components/jc3248_media/`.
- [x] 2.3 Move `stock_firmware/` (images, README and the full `fonts/` set) to
  `docs/factory-firmware/` and merge its README into the restore guide.
- [x] 2.4 Remove `samples/` and every sample-specific path in the browser: the
  size/CRC32 checks, the bad-copy marker, the wrong-file overlay and the sample names in
  the format dialog.
- [x] 2.5 Confirm the ESPHome configuration still resolves and the firmware compiles.

## 3. Repository front page

- [x] 3.1 Rewrite the root `README.md` as a project landing page and documentation
  rubricator.
- [x] 3.2 Update `esphome/README.md` so it no longer refers to the removed native build.
- [x] 3.3 Check every internal link and remove stale paths from `.gitignore` and
  documentation.

## 4. Verification

- [x] 4.1 Run `openspec validate community-docs-restructure`.
- [x] 4.2 Confirm the documentation archive size and repository file sizes.
