#!/usr/bin/env python3
"""Regression tests for WCN3990 firmware staging (no proprietary blobs needed)."""
import importlib.util
import os
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "package_violet", ROOT / "homo" / "scripts" / "package_violet.py")
PACKAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGE)


class RadioFirmwarePackageSmoke(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        base = Path(self.tmp.name)
        PACKAGE.QEAF = str(base / "qeaf")
        PACKAGE.ASSETS = str(base / "assets")
        PACKAGE.BUILD = str(base / "build")
        PACKAGE.STAGE = str(base / "build" / "stage")
        PACKAGE.INIT = str(base / "build" / "homo-init")
        Path(PACKAGE.INIT).parent.mkdir(parents=True)
        Path(PACKAGE.INIT).write_bytes(b"test-init")
        (Path(PACKAGE.ASSETS) / "ios").mkdir(parents=True)
        (Path(PACKAGE.ASSETS) / "ios" / "placeholder.txt").write_text("asset")
        (Path(PACKAGE.QEAF) / "out" / "apps").mkdir(parents=True)
        (Path(PACKAGE.QEAF) / "out" / "stock-firmware").mkdir(parents=True)

    def tearDown(self):
        self.tmp.cleanup()

    def add_radio_files(self):
        fw = Path(PACKAGE.QEAF) / "out" / "stock-firmware"
        image = fw / "vendor" / "firmware_mnt" / "image"
        wifi = fw / "etc" / "wifi"
        image.mkdir(parents=True)
        wifi.mkdir(parents=True)
        for name in ("adsp.mdt", "wlanmdsp.mbn", "bdwlan.bin", "wlan_mac.bin"):
            (image / name).write_bytes(b"real-fixture-data")
        (wifi / "WCNSS_qcom_cfg.ini").write_text("fixture config")

    def test_missing_firmware_does_not_create_empty_mac_or_claim_ready(self):
        PACKAGE.stage_root(False)
        staged = Path(PACKAGE.STAGE)
        self.assertFalse((staged / "lib/firmware/wlan/qca_cld/wlan_mac.bin").exists())
        report = (staged / "etc/qeaf/radio-firmware-status.txt").read_text()
        for required in ("adsp.mdt", "wlanmdsp.mbn", "WCNSS_qcom_cfg.ini", "wlan_mac.bin"):
            self.assertIn(required, report)

    def test_complete_firmware_is_copied_without_zero_byte_placeholders(self):
        self.add_radio_files()
        PACKAGE.stage_root(False)
        staged = Path(PACKAGE.STAGE)
        for name in ("adsp.mdt", "wlanmdsp.mbn", "bdwlan.bin", "wlan_mac.bin"):
            target = staged / "vendor/firmware_mnt/image" / name
            self.assertTrue(target.is_file(), name)
            self.assertGreater(target.stat().st_size, 0, name)
        self.assertTrue((staged / "lib/firmware/wlan/qca_cld/WCNSS_qcom_cfg.ini").is_file())
        report = (staged / "etc/qeaf/radio-firmware-status.txt").read_text()
        self.assertIn("Missing requirements:\n  none", report)


if __name__ == "__main__":
    unittest.main(verbosity=2)
