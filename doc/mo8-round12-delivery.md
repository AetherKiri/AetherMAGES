# MO8 Round 12 Delivery

Round 12 adds native-state Save/Load persistence and the MO8 Save/Load UI.
The implementation provides 48 full-save slots and 48 quick-save slots,
checkpoint and thread restoration, resource reload staging, and 240x135 RGB565
thumbnails. Host persistence is stored in a versioned,
CRC-checked `SAVEDATA_MO8.DAT.host-slots` sidecar with atomic replacement.

The original compressed MO8 save container was not reconstructed. The sidecar
is therefore intentionally host-specific and original-game save
interoperability is not supported.

## Acceptance

Acceptance used the public host input, tick, and frame APIs at 1920x1080. All
Save/Load and cancellation actions were real SDL mouse clicks, including
left-click slot selection and right-click menu cancellation.

- `cancel-fixed2/`: story Save to full slot 02 at checkpoint 40, right-click
  cancellation back to story, story advancement, Load selection and confirmed
  restoration after 1200 ticks.
- `restart-final/`: new process using the same writable directory, title
  Continue -> Load -> full slot 02 -> confirmation, restored after 1200 ticks.
- `slots-final.json`: independent CRC, slot-order, entry checksum, thread
  bounds, and thumbnail validation.

The accepted captures preserve both original-size PPM and lossless PNG output.
Early failed attempts and intermediate black frames remain in the evidence
directory for audit and are not acceptance results.

## Verification

- Release host build and install completed.
- `git diff --check` passed.
- MAGES provider tests passed 4/4.
- Host regression probes compiled successfully.
- Installed FFmpeg dependency hashes match the build overlay.

The runtime/evidence archive is generated alongside this document as
`AetherMAGES-mo8-round12-linux-x86_64.tar.gz` with a companion SHA-256 file.
