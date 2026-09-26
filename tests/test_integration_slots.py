# SPDX-License-Identifier: AGPL-3.0-or-later
"""HostAdmission: transactional capacity ledger, cgroup-aware effective limits, liveness by
flock (reservations die with their holder), explicit refusal on zero/impossible capacity, and
land-over-try priority with anti-starvation.

Tests inject empty cgroup/proc roots plus explicit `limits` so no result depends on the host
this suite runs on; the cgroup readers are exercised separately against synthetic hierarchies.
"""
import os
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "backend"))

from relay_core import integration_slots as S  # noqa: E402

MB = 1 << 20


def make_admission(root: Path, **kw) -> S.HostAdmission:
    empty = root / "empty"
    (empty / "proc").mkdir(parents=True, exist_ok=True)
    (empty / "cgroup").mkdir(parents=True, exist_ok=True)
    kw.setdefault("limits", {"cpus": 2.0, "memory_bytes": 100 * MB, "disk_bytes": 100 * MB})
    kw.setdefault("reserve_memory_bytes", 0)
    kw.setdefault("reserve_disk_bytes", 0)
    kw.setdefault("poll_interval", 0.05)
    kw.setdefault("cgroup_root", empty / "cgroup")
    kw.setdefault("proc_root", empty / "proc")
    return S.HostAdmission(state_root=root / "state", **kw)


class AdmissionTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.adm = make_admission(self.root)

    def tearDown(self):
        self.tmp.cleanup()

    def test_acquire_release_status(self):
        with self.adm.acquire("repo1", "job1", memory_bytes=MB, disk_bytes=MB, cpus=1) as r:
            self.assertEqual(r.repo_id, "repo1")
            st = self.adm.status()
            self.assertEqual(len(st["reservations"]), 1)
            self.assertEqual(st["usage"]["cpus"], 1.0)
            self.assertEqual(st["capacity"]["cpus"], 2.0)
            self.assertIn("live", st)
        st = self.adm.status()
        self.assertEqual(st["reservations"], [])
        self.assertEqual(st["usage"]["cpus"], 0)

    def test_release_is_idempotent(self):
        r = self.adm.acquire("repo1", "job1")
        r.release()
        r.release()
        self.assertEqual(self.adm.status()["reservations"], [])

    def test_contended_timeout_zero_refuses(self):
        with self.adm.acquire("repo1", "job1", cpus=2):
            with self.assertRaises(S.AdmissionRefused) as ctx:
                self.adm.acquire("repo1", "job2", cpus=1, timeout=0)
            self.assertIn("cpu ledger full", str(ctx.exception))
        # capacity freed: now it fits
        with self.adm.acquire("repo1", "job2", cpus=1):
            pass

    def test_zero_capacity_refuses_immediately(self):
        adm = make_admission(self.root / "zc", limits={"cpus": 0.0})
        started = time.monotonic()
        with self.assertRaises(S.AdmissionRefused) as ctx:
            adm.acquire("repo1", "job1", cpus=1, timeout=5)  # even a timeout must not wait
        self.assertLess(time.monotonic() - started, 2)
        self.assertIn("can never fit", str(ctx.exception))

    def test_impossible_request_refuses_even_with_timeout(self):
        with self.assertRaises(S.AdmissionRefused):
            self.adm.acquire("repo1", "job1", memory_bytes=10 * 100 * MB, timeout=5)

    def test_timeout_exit_code_matches_land_busy(self):
        with self.adm.acquire("repo1", "job1", cpus=2):
            started = time.monotonic()
            with self.assertRaises(S.AdmissionTimeout) as ctx:
                self.adm.acquire("repo1", "job2", cpus=1, timeout=0.3)
            self.assertGreaterEqual(time.monotonic() - started, 0.25)
            self.assertEqual(ctx.exception.exit_code, 7)
        # the timed-out waiter left no waiter row behind
        self.assertEqual(self.adm.status()["waiters"], [])

    def test_memory_ledger(self):
        with self.adm.acquire("repo1", "job1", memory_bytes=90 * MB):
            with self.assertRaises(S.AdmissionRefused):
                self.adm.acquire("repo1", "job2", memory_bytes=20 * MB, timeout=0)

    def test_unknown_memory_capacity_refuses_blind_admission(self):
        adm = make_admission(self.root / "nomem",
                             limits={"cpus": 2.0, "disk_bytes": 100 * MB})
        with self.assertRaises(S.AdmissionRefused) as ctx:
            adm.acquire("repo1", "job1", memory_bytes=MB, timeout=0)
        self.assertIn("unknown", str(ctx.exception))

    def test_bad_arguments_refuse(self):
        with self.assertRaises(S.AdmissionRefused):
            self.adm.acquire("repo1", "job1", priority="urgent")
        with self.assertRaises(S.AdmissionRefused):
            self.adm.acquire("repo1", "job1", memory_bytes=-1)

    def test_process_death_releases_reservation(self):
        script = (
            "import sys, time\n"
            f"sys.path.insert(0, {str(Path(__file__).resolve().parents[1] / 'backend')!r})\n"
            "from relay_core import integration_slots as S\n"
            f"adm = S.HostAdmission(state_root={str(self.root / 'state')!r},"
            f" limits={{'cpus': 2.0, 'memory_bytes': 100 * {MB}, 'disk_bytes': 100 * {MB}}},"
            " reserve_memory_bytes=0, reserve_disk_bytes=0,"
            f" cgroup_root={str(self.root / 'empty' / 'cgroup')!r},"
            f" proc_root={str(self.root / 'empty' / 'proc')!r})\n"
            "r = adm.acquire('repo1', 'child', cpus=2)\n"
            "print('acquired', flush=True)\n"
            "time.sleep(60)\n")
        proc = subprocess.Popen([sys.executable, "-c", script],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            assert proc.stdout.readline().strip() == b"acquired"
            with self.assertRaises(S.AdmissionRefused):
                self.adm.acquire("repo1", "job2", cpus=1, timeout=0)
            proc.kill()  # SIGKILL: the kernel must drop the flock
            proc.wait()
            deadline = time.monotonic() + 10
            while True:
                try:
                    with self.adm.acquire("repo1", "job2", cpus=1, timeout=0):
                        break
                except S.AdmissionRefused:
                    if time.monotonic() > deadline:
                        raise
                    time.sleep(0.05)
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait()

    def test_land_outranks_try_but_try_is_not_starved(self):
        blocker = self.adm.acquire("repo1", "blocker", cpus=2)
        got: list[str] = []

        def try_job():
            with self.adm.acquire("repo1", "try", cpus=1, priority="try", timeout=10):
                got.append("try")
                time.sleep(0.2)

        t = threading.Thread(target=try_job)
        t.start()
        time.sleep(0.2)  # let the try waiter register
        # register a land waiter, then free capacity: the try waiter must yield
        land_result: dict = {}

        def land_job():
            with self.adm.acquire("repo1", "land", cpus=2, priority="land", timeout=10):
                land_result["ok"] = True

        lt = threading.Thread(target=land_job)
        lt.start()
        time.sleep(0.2)  # both waiters registered
        blocker.release()
        lt.join(timeout=10)
        self.assertTrue(land_result.get("ok"))
        self.assertEqual(got, [])  # try still waiting while land holds capacity
        t.join(timeout=10)
        self.assertEqual(got, ["try"])

    def test_aged_try_outranks_fresh_land(self):
        adm = make_admission(self.root / "starve", starve_seconds=0.0)
        blocker = adm.acquire("repo1", "blocker", cpus=2)
        order: list[str] = []

        def try_job():
            with adm.acquire("repo1", "try", cpus=2, priority="try", timeout=10):
                order.append("try")

        def land_job():
            with adm.acquire("repo1", "land", cpus=2, priority="land", timeout=10):
                order.append("land")

        t = threading.Thread(target=try_job)
        t.start()
        time.sleep(0.2)
        lt = threading.Thread(target=land_job)
        lt.start()
        time.sleep(0.2)
        blocker.release()
        t.join(timeout=10)
        lt.join(timeout=10)
        self.assertEqual(order, ["try", "land"])  # starve_seconds=0 promotes the aged try


class CgroupParsingTests(unittest.TestCase):
    """Synthetic v2 hierarchies: the tightest ancestor limit wins, and `current` is read at
    the node that carries the binding limit."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def _adm(self, *, cgroup_line="0::/app.slice/relay.service/scope-1\n",
             meminfo="MemTotal:       67108864 kB\nMemAvailable:   33554432 kB\n",
             **kw) -> S.HostAdmission:
        proc = self.root / "proc"
        (proc / "self").mkdir(parents=True, exist_ok=True)
        (proc / "self" / "cgroup").write_text(cgroup_line)
        (proc / "meminfo").write_text(meminfo)
        # no memory/cpu limit overrides: detection is what these tests exercise
        kw.setdefault("limits", {"disk_bytes": 100 * MB})
        kw.setdefault("cgroup_root", self.root / "cgroup")
        kw.setdefault("proc_root", proc)
        kw.setdefault("reserve_memory_bytes", 0)
        kw.setdefault("reserve_disk_bytes", 0)
        kw.setdefault("poll_interval", 0.05)
        return S.HostAdmission(state_root=self.root / "state", **kw)

    def test_tightest_ancestor_memory_wins(self):
        cg = self.root / "cgroup"
        (cg / "app.slice" / "relay.service" / "scope-1").mkdir(parents=True)
        (cg / "memory.max").write_text("max\n")
        (cg / "app.slice" / "memory.max").write_text(str(8 << 30) + "\n")        # 8 GiB
        (cg / "app.slice" / "memory.current").write_text(str(1 << 30) + "\n")
        (cg / "app.slice" / "relay.service" / "memory.max").write_text(str(4 << 30) + "\n")
        (cg / "app.slice" / "relay.service" / "memory.current").write_text(str(2 << 30) + "\n")
        adm = self._adm()
        limit, current = adm._cgroup_memory()
        self.assertEqual(limit, 4 << 30)       # tightest ancestor, not the root "max"
        self.assertEqual(current, 2 << 30)     # usage at the node carrying that limit
        self.assertEqual(adm.capacity().memory_bytes, 4 << 30)

    def test_tightest_ancestor_cpu_wins(self):
        cg = self.root / "cgroup"
        (cg / "app.slice" / "relay.service" / "scope-1").mkdir(parents=True)
        (cg / "cpu.max").write_text("max 100000\n")
        (cg / "app.slice" / "cpu.max").write_text("400000 100000\n")   # 4 cpus
        (cg / "app.slice" / "relay.service" / "cpu.max").write_text("200000 100000\n")  # 2
        adm = self._adm()
        self.assertEqual(adm._cgroup_cpus(), 2.0)

    def test_no_v2_falls_back_to_meminfo(self):
        adm = self._adm(cgroup_line="2:memory:/user.slice\n",  # no unified line
                        meminfo="MemTotal:       16777216 kB\nMemAvailable:   8388608 kB\n")
        self.assertEqual(adm._cgroup_v2_chain(), [])  # no unified hierarchy
        self.assertEqual(adm.capacity().memory_bytes, 16 << 30)


if __name__ == "__main__":
    unittest.main()
