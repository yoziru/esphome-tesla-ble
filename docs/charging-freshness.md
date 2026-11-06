# Native charging-power freshness

The optional `packages/charging-freshness.yml` package exposes a
`Charger Power Received` timestamp alongside the native charger-power sensor.
It requires `packages/common.yml`, which supplies the `homeassistant_time` clock.
Standard configurations already include it through `packages/base.yml`.
Include the freshness package after `packages/client.yml` and rebuild the bridge
firmware. For a custom configuration, the relevant package order is:

```yaml
packages:
  common: !include packages/common.yml
  client: !include packages/client.yml
  freshness: !include packages/charging-freshness.yml
```

Keep the rest of your board, network and client substitutions as usual. The
complete build fixtures are `tests/charging_freshness.yml` (ESP32) and
`tests/charging_freshness_s3.yml` (ESP32-S3).

Repeated native power publication is enabled only when the optional
`tesla_ble_vehicle.charger_power` configuration is present (as in this package).
It fires raw and normal value callbacks and sends sensor API updates on each
valid native power report, even when unchanged. Without this configuration,
duplicate-value suppression is unchanged. The native sensor keeps its generated
`tesla_charger_power_sensor` ID when overrides are enabled.

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
