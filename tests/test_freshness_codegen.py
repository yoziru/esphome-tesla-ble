#!/usr/bin/env python3
"""Generate real ESPHome C++ for the default and optional receipt packages."""

from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def generate(fixture, device_name):
    config = ROOT / "tests" / fixture
    # Keep codegen isolated from firmware builds and their cached artifacts.
    with tempfile.TemporaryDirectory(prefix="tesla-ble-codegen-") as directory:
        subprocess.run(
            [sys.executable, "-m", "esphome", "compile", str(config), "--only-generate"],
            env={**os.environ, "ESPHOME_DATA_DIR": directory, "ESPHOME_BUILD_PATH": "build"},
            check=True,
        )
        source = (Path(directory) / "build" / device_name / "src" / "main.cpp").read_text()
    return re.sub(r"^\s*//.*$", "", source, flags=re.M)


def main():
    for fixture, device_name, enabled in (
        ("charging_power_base.yml", "tesla-ble-power-default-test", False),
        ("charging_freshness.yml", "tesla-ble-freshness-test", True),
        ("charging_freshness_s3.yml", "ble-freshness-s3", True),
    ):
        source = generate(fixture, device_name)
        assert re.search(r"sensor::Sensor \*const tesla_charger_power_sensor\b", source), fixture
        assert 'set_sensor("charger_power", tesla_charger_power_sensor)' in source, fixture
        expected = str(enabled).lower()
        assert f"set_charger_power_force_report({expected})" in source, fixture
        if enabled:
            # The actual package lambda must be attached to the native sensor,
            # use the shared clock and publish to this bridge's receipt entity.
            callback = re.search(
                r"tesla_charger_power_sensor->add_on_raw_state_callback\(TriggerForwarder<float>\{(\w+)\}\)",
                source,
            )
            assert callback, fixture
            actions = re.search(callback[1] + r"->add_actions\(\{(\w+)\}\)", source)
            assert actions, fixture
            action = actions[1]
            assert f"new({action}) text_sensor::TextSensorPublishAction<float>(charger_power_received)" in source, fixture
            assert re.search(
                action + r"->set_state\(\[\]\(float x\) -> std::string \{.*?homeassistant_time->utcnow\(\)",
                source, re.S,
            ), fixture
            assert '!received.is_valid()' in source, fixture
            assert 'std::isfinite(x)' in source, fixture
            assert 'return std::string("unavailable")' in source, fixture
            assert 'received.strftime("%Y-%m-%dT%H:%M:%SZ")' in source, fixture
        else:
            assert "charger_power_received" not in source, fixture
    print("OK: default and opt-in IDs, raw callback, shared clock and timestamp codegen")


if __name__ == "__main__":
    main()
