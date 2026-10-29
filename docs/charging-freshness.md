# Native charging-power freshness

The optional `packages/charging-freshness.yml` package exposes a
`Charger Power Received` timestamp alongside the native charger-power sensor.
Include it after the client package and rebuild the bridge firmware.

The timestamp state value records receipt of a valid native power field, including
unchanged values. Missing power fields do not refresh it. It is not advanced by
a timer, estimated power, other vehicle fields, or an HA API replay. An invalid
clock produces `unavailable`. The existing native validity guard rejects invalid
power before publication, leaving its last valid receipt to expire.

Consumers must check the timestamp's state value and the matching numeric power
sensor. Its HA `last_updated`/`last_reported` can reflect a reconnect replay.
Custom filters which delay or suppress valid numeric values can separate the raw
receipt from the filtered reading; use the unmodified native sensor for control.

PowerSync uses this receipt to keep confirmed steady charging visible while
retaining its 90-second expiry when native measurements stop. Each bridge needs
its own timestamp entity. Firmware compilation does not prove live delivery;
verify repeated samples and loss of native power reports on the deployed bridge.
