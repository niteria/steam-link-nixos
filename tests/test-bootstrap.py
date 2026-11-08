#!/usr/bin/env python3
"""Exercise the actual bootstrap renderer with independent synthetic identities."""
import os
from pathlib import Path
import subprocess
import sys
import unittest
from unittest.mock import patch

ROOT = Path(sys.argv.pop(1))


def render(**updates):
    environment = {
        "STATE_NONCE": "fedcba9876543210",
        "ROOT_SNAPSHOT": "b" * 64,
        "RECOVERY_USB_SERIAL": "SYNTHETIC_DRIVE",
        "EXPECTED_BAD_BLOCKS": "3",
    } | updates
    scope = {"os": os}
    with patch.dict(os.environ, environment):
        for name in ("internal/writable/init-transform.py", "internal/reclaim/init-transform.py"):
            exec(compile((ROOT / name).read_text(), name, "exec"), scope)
    return scope["script"]


class BootstrapTests(unittest.TestCase):
    def test_nondefault_identity_and_full_uint64(self):
        script = render(STATE_NONCE="ffffffffffffffff")
        self.assertIn("state_nonce=0xffffffffffffffff", script)
        self.assertIn('" = 18446744073709551615 ] || fail nonce', script)
        self.assertIn("b" * 64 + " ffffffffffffffff root-b 1800", script)
        self.assertIn("= SYNTHETIC_DRIVE ]; then exec reboot -f", script)
        self.assertIn('bad_peb_count)" = 3', script)
        self.assertNotIn("@", script)
        subprocess.run(["bash", "-n"], input=script, text=True, check=True)

    def test_missing_recovery_drive_holds(self):
        script = render(RECOVERY_USB_SERIAL="")
        self.assertNotIn("exec reboot -f", script)
        self.assertIn("hold recovery-usb-unavailable", script)
        self.assertNotIn("@", script)
        subprocess.run(["bash", "-n"], input=script, text=True, check=True)

    def test_invalid_identity_refused(self):
        for update in ({"STATE_NONCE": "12"}, {"STATE_NONCE": "g" * 16},
                       {"ROOT_SNAPSHOT": "bad"}, {"RECOVERY_USB_SERIAL": "x; reboot"},
                       {"EXPECTED_BAD_BLOCKS": "-1"}, {"EXPECTED_BAD_BLOCKS": "4097"}):
            with self.subTest(update=update), self.assertRaises(AssertionError):
                render(**update)

    def test_cleanup_and_reserve_contract(self):
        script = render()
        self.assertIn('-b 60 --disable-fastmap', script)
        self.assertIn('reserved_for_bad)" -ge 160', script)
        self.assertLess(script.index("rmmod steamlink_reclaim_pool"), script.index("rmmod steamlink_marvell_reclaim"))
        self.assertLess(script.index("umount /newroot"), script.index("ubidetach -d 0"))
        self.assertNotIn("ubiformat", script)
        self.assertIn("readlink -f /nix/var/nix/profiles/system", script)


unittest.main()
