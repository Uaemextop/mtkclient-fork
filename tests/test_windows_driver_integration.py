import sys
import types
import unittest
from unittest import mock

fake_fuse = types.ModuleType("fuse")
fake_fuse.Operations = type("FakeOperations", (), {})
fake_fuse.LoggingMixIn = type("FakeLoggingMixIn", (), {})
sys.modules.setdefault("fuse", fake_fuse)

from mtkclient.config.usb_ids import default_ids  # noqa: E402
from mtkclient.Library.mtk_class import Mtk  # noqa: E402


class WindowsDriverIntegrationTests(unittest.TestCase):
    def test_usb_id_tables_are_nested_dicts(self):
        expected = {
            0x0FCE: 0xF200,
            0x0403: 0x6001,
            0x1A86: 0x55D3,
            0x4348: 0x5523,
            0x10C4: 0xEA60,
            0x11CA: 0x0211,
        }

        for vendor_id, product_id in expected.items():
            self.assertIsInstance(default_ids[vendor_id], dict)
            self.assertEqual(default_ids[vendor_id][product_id], -1)

    def test_windows_defaults_to_detect_serial_port(self):
        config = types.SimpleNamespace(
            gui=False,
            loader=None,
            vid=0x0E8D,
            pid=0x0003,
            interface=-1,
        )

        with mock.patch.object(sys, "platform", "win32"):
            client = Mtk(config=config, preinit=False)

        self.assertEqual(client.serialportname, "DETECT")

    def test_setup_uses_nested_portconfig_for_single_device(self):
        config = types.SimpleNamespace(
            gui=False,
            loader=None,
            vid=-1,
            pid=-1,
            interface=-1,
        )
        client = Mtk(config=config, preinit=False)

        with mock.patch("mtkclient.Library.mtk_class.Port") as port_cls, \
                mock.patch("mtkclient.Library.mtk_class.Preloader"), \
                mock.patch("mtkclient.Library.mtk_class.DAloader"):
            client.setup(vid=0x1004, pid=0x6000, interface=-1)

        port_kwargs = port_cls.call_args.kwargs
        self.assertEqual(port_kwargs["portconfig"], {0x1004: {0x6000: 2}})


if __name__ == "__main__":
    unittest.main()
