# Design

## Context

GitHub search weighs the repository name, description, topics and README text. The name
already carries `jc3248w535` and `esphome`; the description and topics can carry the board
aliases (`JC3248W535C`, `JC3248W535EN`), the panel (`AXS15231B`), the format (`MJPEG`) and
the integration (`Home Assistant`).

## Goals / Non-Goals

**Goals:**

- Put the model aliases, panel name and primary use case in the first screen of the README.
- Use the full topic budget for relevant, non-spammy keywords.
- Keep the README scannable; long reference material moves to `docs/usage.md`.

**Non-Goals:**

- Do not change firmware, entity names or release assets.
- Do not add keyword stuffing or unrelated topics.

## Decisions

- Description: one sentence naming the aliases, the AXS15231B panel, MJPEG/AVI, microSD
  over USB, Home Assistant and OTA.
- Topics: the existing six plus ESP32 variants, board aliases, panel, display types,
  player/storage terms, ESP-IDF, Home Assistant dashboard and IoT.
- README structure: hero, one-paragraph pitch, six features, screenshots, install,
  hardware, documentation table, dashboard cards, short limitations, credits, license.
- `docs/usage.md` owns the UI walkthrough, media conversion, entity table, card notes and
  the full limitation list.

## Risks / Trade-offs

- GitHub descriptions are capped at 350 characters; the chosen text stays below it.
- Topic limit is 20; adding more requires dropping an existing one.

## Migration Plan

1. Rewrite README and add `docs/usage.md`.
2. Update the repository description and topics.
3. Amend the single release commit and force-push main and the release tag.

## Open Questions

None.
