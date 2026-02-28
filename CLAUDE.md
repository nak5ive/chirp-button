# Chirp Button — Claude Code Rules

## Spec / Firmware Sync Rule

`SPEC.md` is the **source of truth** for all behavior, hardware, and implementation decisions.

- Any change to `SPEC.md` **must** be reflected in the firmware before the work is considered complete.
- Any change to the firmware **must** be reflected in `SPEC.md` before the work is considered complete.
- If spec and firmware ever conflict, treat `SPEC.md` as authoritative and update the firmware to match.
- Do not mark a task done if spec and firmware are out of sync.
