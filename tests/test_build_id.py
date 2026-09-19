"""scripts/build-id.py: the date, the 24-hour hour, then .01, .02, … for each build of the app that hour."""
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("build_id", ROOT / "scripts" / "build-id.py")
build_id = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build_id)


class BuildIdTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.dir = self.temp.name

    def tearDown(self):
        self.temp.cleanup()

    def sidecar(self):
        return (Path(self.dir) / build_id.SIDECAR).read_text().strip()

    def test_the_first_build_of_an_hour_is_01_and_the_next_is_02(self):
        self.assertEqual(build_id.next_id(self.dir, "2026-09-19.14H"), "2026-09-19.14H.01")
        self.assertEqual(self.sidecar(), "2026-09-19.14H.01")
        self.assertEqual(build_id.next_id(self.dir, "2026-09-19.14H"), "2026-09-19.14H.02")
        self.assertEqual(self.sidecar(), "2026-09-19.14H.02")

    def test_a_new_hour_and_a_new_day_start_again_at_01(self):
        for _ in range(3):
            build_id.next_id(self.dir, "2026-09-19.14H")
        self.assertEqual(build_id.next_id(self.dir, "2026-09-19.15H"), "2026-09-19.15H.01")
        self.assertEqual(build_id.next_id(self.dir, "2026-09-20.15H"), "2026-09-20.15H.01")

    def test_the_hour_is_the_24_hour_clock_with_two_digits(self):
        import datetime
        self.assertEqual(build_id.hour_of(datetime.datetime(2026, 9, 10, 14, 5)), "2026-09-10.14H")
        self.assertEqual(build_id.hour_of(datetime.datetime(2026, 9, 10, 7, 59)), "2026-09-10.07H")
        self.assertEqual(build_id.hour_of(datetime.datetime(2026, 9, 10, 0, 0)), "2026-09-10.00H")

    def test_past_99_the_number_grows_a_digit(self):
        (Path(self.dir) / build_id.SEQUENCE).write_text(json.dumps({"hour": "2026-09-19.14H", "n": 99}))
        self.assertEqual(build_id.next_id(self.dir, "2026-09-19.14H"), "2026-09-19.14H.100")

    def test_a_damaged_count_starts_the_hour_again(self):
        (Path(self.dir) / build_id.SEQUENCE).write_text("not json")
        self.assertEqual(build_id.next_id(self.dir, "2026-09-19.14H"), "2026-09-19.14H.01")
        (Path(self.dir) / build_id.SEQUENCE).write_text(json.dumps({"hour": "2026-09-19.14H", "n": "7"}))
        self.assertEqual(build_id.next_id(self.dir, "2026-09-19.14H"), "2026-09-19.14H.01")

    def test_the_command_line_prints_the_id_and_refuses_bad_input(self):
        self.assertEqual(build_id.main([self.dir, "--now", "2026-09-10T14"]), 0)
        self.assertEqual(self.sidecar(), "2026-09-10.14H.01")
        self.assertEqual(build_id.main([self.dir, "--now", "yesterday"]), 1)
        self.assertEqual(build_id.main([self.dir + "/nowhere"]), 1)


if __name__ == "__main__":
    unittest.main()
