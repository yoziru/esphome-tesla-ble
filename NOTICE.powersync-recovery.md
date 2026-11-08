# Optional PowerSync recovery adapter

`components/tesla_ble_vehicle/powersync_recovery.cpp` and
`powersync_recovery_policy.h` are AGPL-3.0-only and derive from PowerSync's
producer-owned recovery component. The existing source and licence notices
remain applicable to the other component files. The TeslaBLE library is also
AGPL-3.0-only.

The adapter is enabled only when `recovery_proof` is configured. It requires a
matching TeslaBLE revision exposing correlated native charge reports. Existing
configurations without the optional recovery field retain their original
control behaviour and remain compatible with the existing library API.

Recovery records current-state convergence; it does not reconstruct an old
command outcome or claim a vehicle queue-drain guarantee. The operator must
use the complete PowerSync inventory transaction to release a held bridge.
