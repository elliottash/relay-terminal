# SPDX-License-Identifier: GPL-3.0-or-later
"""scripts/relay-update.py decides, offline, which package a machine takes.

The script talks to GitHub only at run time; everything that decides the outcome —
which distribution tag an /etc/os-release maps to, which dpkg architecture a uname
is, which Version: a tag becomes, which asset name it publishes as — is a pure
function, and this is what keeps the download aimed at the right file. dpkg's own
--compare-versions does the ordering, so the beta tilde sorts as dpkg sorts it.
"""
import importlib.util
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCRIPT = ROOT / "scripts" / "relay-update.py"


def load_updater():
    spec = importlib.util.spec_from_file_location("relay_update", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class UpdateScriptTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.updater = load_updater()

    def test_distro_slug_matches_the_release_assets(self):
        ubuntu = 'NAME="Ubuntu"\nVERSION="24.04 LTS"\nID=ubuntu\nVERSION_ID="24.04"\n'
        self.assertEqual(self.updater.distro_slug(ubuntu), "ubuntu24.04")
        trixie = 'PRETTY_NAME="Debian GNU/Linux 13 (trixie)"\nID=debian\nVERSION_ID="13"\n'
        self.assertEqual(self.updater.distro_slug(trixie), "debian13")
        self.assertEqual(self.updater.distro_slug("ID=ubuntu\nVERSION_ID=26.04\n"), "ubuntu26.04")

    def test_distro_slug_refuses_what_has_no_package(self):
        self.assertIsNone(self.updater.distro_slug("ID=fedora\nVERSION_ID=42\n"))
        self.assertIsNone(self.updater.distro_slug("ID=ubuntu\n"))          # no VERSION_ID
        self.assertIsNone(self.updater.distro_slug(""))                     # no os-release at all

    def test_arch_names_match_dpkg(self):
        self.assertEqual(self.updater.deb_arch("x86_64"), "amd64")
        self.assertEqual(self.updater.deb_arch("aarch64"), "arm64")
        self.assertEqual(self.updater.deb_arch("riscv64"), "riscv64")       # passed through

    def test_tag_becomes_the_package_version(self):
        # The ~ keeps a beta below the final in dpkg's ordering (docs/RELEASING.md).
        self.assertEqual(self.updater.upstream_version("v0.1.0-beta.3"), "0.1.0~beta.3")
        self.assertEqual(self.updater.upstream_version("v0.1.0"), "0.1.0")
        self.assertEqual(self.updater.full_version("v0.1.0-beta.3", "ubuntu24.04"),
                         "0.1.0~beta.3-1~ubuntu24.04")
        self.assertEqual(self.updater.full_version("v0.1.0", "debian13"), "0.1.0-1~debian13")

    def test_asset_name_keeps_the_tag_spelling(self):
        # GitHub rewrites "~" in asset names, so the file keeps the dash (release.yml).
        self.assertEqual(self.updater.asset_name("v0.1.0-beta.3", "ubuntu24.04", "amd64"),
                         "relay_0.1.0-beta.3_ubuntu24.04_amd64.deb")
        self.assertEqual(self.updater.asset_name("v0.1.0", "debian13", "arm64"),
                         "relay_0.1.0_debian13_arm64.deb")

    def test_checksum_is_read_for_the_right_file(self):
        sums = ("0f00… relay_0.1.0-beta.3_ubuntu24.04_amd64.deb\n"
                "aa11… relay_0.1.0-beta.3_debian13_arm64.deb\n"
                "bb22… relay-0.1.0-beta.3.tar.gz\n")
        self.assertEqual(self.updater.verified_checksum(sums, "relay_0.1.0-beta.3_debian13_arm64.deb"),
                         "aa11…")
        self.assertIsNone(self.updater.verified_checksum(sums, "relay_0.1.0-beta.3_ubuntu26.04_amd64.deb"))

    def test_dpkg_orders_the_versions_the_script_compares(self):
        older = "0.1.0~beta.2-1~ubuntu24.04"
        newer = "0.1.0~beta.3-1~ubuntu24.04"
        final = "0.1.0-1~ubuntu24.04"
        self.assertTrue(self.updater.apt_newer(older, newer))
        self.assertFalse(self.updater.apt_newer(newer, newer))
        self.assertFalse(self.updater.apt_newer(final, newer))
        self.assertTrue(self.updater.apt_newer(newer, final))

    def test_install_refuses_a_machine_without_the_package(self):
        # A source checkout must be told it is one, not offered a .deb that would shadow it.
        self.assertIsNone(self.updater.installed_deb_version(package="relay-not-a-package"))


if __name__ == "__main__":
    unittest.main()
