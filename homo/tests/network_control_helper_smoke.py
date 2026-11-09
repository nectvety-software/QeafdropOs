"""Exercise fixed helper operations with mocked system commands; no network changes."""
import io
from pathlib import Path
import types
import unittest
from unittest.mock import patch

helper = types.ModuleType("hachimi_network_helper_test")
source = Path(__file__).resolve().parents[1] / "platform/linux/hachimi-network"
exec(compile(source.read_text(encoding="utf-8"), str(source), "exec"), helper.__dict__)


class EthernetControls(unittest.TestCase):
    def operation(self, operation, paths):
        with patch.object(helper.sys, "stdin", io.StringIO(operation + "\n")), \
                patch.object(helper.glob, "glob", return_value=paths), \
                patch.object(helper, "run", return_value="") as run, \
                patch.object(helper, "emit"):
            helper.main()
            return run.call_args.args[0]

    def test_eth2_is_preferred_and_only_link_state_changes(self):
        paths = ["/sys/class/net/eth0", "/sys/class/net/eth2"]
        self.assertEqual(self.operation("ethernet-off", paths), ["ip", "link", "set", "dev", "eth2", "down"])
        self.assertEqual(self.operation("ethernet-on", paths), ["ip", "link", "set", "dev", "eth2", "up"])

    def test_first_available_interface_is_used(self):
        self.assertEqual(self.operation("ethernet-on", ["/sys/class/net/eth1"])[4], "eth1")

    def test_absent_device_and_untrusted_interface_are_rejected(self):
        for paths in ([], ["/sys/class/net/eth2;anything"]):
            with self.assertRaises(RuntimeError):
                self.operation("ethernet-on", paths)

    def test_extra_arguments_are_rejected(self):
        with self.assertRaises(ValueError):
            self.operation("ethernet-off\teth0", ["/sys/class/net/eth2"])



class BluetoothControls(unittest.TestCase):
    def test_mac_validation(self):
        self.assertEqual(helper.bt_mac('a0:b1:c2:d3:e4:f5'), 'A0:B1:C2:D3:E4:F5')
        for value in ('not-a-mac', '00:11:22:33:44', '00:11:22:33:44:ZZ'):
            with self.assertRaises(ValueError):
                helper.bt_mac(value)

    def test_scan_emits_only_real_devices_from_bluez(self):
        emitted = []
        def ctl(*args, **kwargs):
            if args == ('devices',):
                return 'Device AA:BB:CC:DD:EE:FF Headphones\nDevice invalid Fake\n'
            if args == ('info', 'AA:BB:CC:DD:EE:FF'):
                return 'Device AA:BB:CC:DD:EE:FF\n\tPaired: yes\n\tConnected: no\n'
            return 'Discovery started'
        with patch.object(helper, 'bluetoothctl', side_effect=ctl), \
                patch.object(helper.time, 'sleep'), patch.object(helper, 'emit', side_effect=lambda *v: emitted.append(v)):
            helper.bluetooth_scan()
        self.assertIn(('BTDEV', 'AA:BB:CC:DD:EE:FF', -1, 1, 0, helper.hx('Headphones')), emitted)
        self.assertTrue(any(row[0] == 'OK' for row in emitted))

    def test_pair_operation_validates_address_before_command(self):
        with patch.object(helper, 'bluetoothctl') as ctl:
            with self.assertRaises(ValueError):
                helper.bluetooth_operation('bt-pair', ['invalid'])
            ctl.assert_not_called()

if __name__ == "__main__":
    unittest.main()
