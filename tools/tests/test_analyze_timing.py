import os, sys, tempfile, time, unittest
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import analyze_timing as at
import logger


def log(lines):
    f = tempfile.NamedTemporaryFile("w", suffix=".log", delete=False, encoding="utf-8")
    for i, ln in enumerate(lines):
        f.write("2026-10-19T10:00:%02d.000 | %s\n" % (i % 60, ln))
    f.close()
    return f.name


def M(name, us, state=None, fault=None):
    return "MARK %s %d" % (name, us) + ("" if state is None else " state=%d fault=%d" % (state, fault))


class Analyze(unittest.TestCase):
    def check(self, lines, fn, *args):
        p = log(lines)
        try:
            return fn(at.parse(p), *args)
        finally:
            os.remove(p)

    def test_grace_a_ok_and_wrap(self):
        base = 0xFFFFFFFF - 10_000_000                       # micros() wraps during the 25 s
        ok, _ = self.check([M("GRACE_START", base), M("CONFIRM", (base + 25_100_000) & 0xFFFFFFFF, 2, 0)], at.check_grace, 25.0, "a")
        self.assertTrue(ok)

    def test_grace_a_too_early_fails(self):
        ok, _ = self.check([M("GRACE_START", 1_000_000), M("CONFIRM", 25_000_000, 2, 0)], at.check_grace, 25.0, "a")
        self.assertFalse(ok)

    def test_grace_b_needs_cancel_and_no_confirm(self):
        ok, _ = self.check([M("GRACE_START", 1), M("GRACE_CANCEL", 10_000_000)], at.check_grace, 25.0, "b")
        self.assertTrue(ok)
        ok, _ = self.check([M("GRACE_START", 1), M("CONFIRM", 26_000_000, 2, 0)], at.check_grace, 25.0, "b")
        self.assertFalse(ok)

    def test_grace_c_37_from_first_change(self):
        lines = [M("GRACE_START", 0), M("GRACE_CANCEL", 10_000_000), M("GRACE_START", 12_000_000), M("CONFIRM", 37_050_000, 2, 0)]
        ok, _ = self.check(lines, at.check_grace, 25.0, "c")
        self.assertTrue(ok)
        lines[-1] = M("CONFIRM", 36_000_000, 2, 0)
        ok, _ = self.check(lines, at.check_grace, 25.0, "c")
        self.assertFalse(ok)

    def test_grace_d_and_boot_is_ignored_by_default(self):
        lines = [M("GRACE_START", 0), M("CONFIRM_BOOT", 25_000_000, 1, 0), M("GRACE_START", 30_000_000), M("CONFIRM", 40_020_000, 2, 0)]
        ok, out = self.check(lines, at.check_grace, 10.0, "d")
        self.assertTrue(ok, out)

    def test_settle_modes(self):
        ok, _ = self.check([M("RAW", 1000, 2, 0), M("CAND", 1000, 2, 0)], at.check_settle, "s0")
        self.assertTrue(ok)
        ok, _ = self.check([M("RAW", 1000, 2, 0), M("CAND", 90_000, 2, 0)], at.check_settle, "s0")
        self.assertFalse(ok)
        ok, _ = self.check([M("RAW", 0, 2, 0), M("RAW", 400_000, 1, 0)], at.check_settle, "pulse")
        self.assertTrue(ok)
        ok, _ = self.check([M("RAW", 0, 2, 0), M("CAND", 500_000, 2, 0)], at.check_settle, "hold")
        self.assertTrue(ok)
        ok, _ = self.check([M("RAW", 0, 2, 0), M("CAND", 700_000, 2, 0)], at.check_settle, "hold")
        self.assertFalse(ok)

    def test_fault_both_directions(self):
        lines = [M("COLD_START", 0, 1, 0), M("RAW", 1_000_000, 1, 1), M("CONFIRM", 26_000_000, 1, 1),
                 M("RAW", 30_000_000, 2, 0), M("CONFIRM", 55_020_000, 2, 0)]
        ok, out = self.check(lines, at.check_fault, 25.0)
        self.assertTrue(ok, out)
        lines[2] = M("CONFIRM", 20_000_000, 1, 1)
        ok, _ = self.check(lines, at.check_fault, 25.0)
        self.assertFalse(ok)

    def test_step_filter(self):
        p = log([M("RAW", 0, 2, 0), "# STEP x begin", M("RAW", 5, 2, 0), "# STEP x end", M("RAW", 9, 2, 0)])
        try:
            self.assertEqual([m[1] for m in at.parse(p, "x")], [5])
            self.assertEqual(len(at.parse(p)), 3)
        finally:
            os.remove(p)


class Logger(unittest.TestCase):
    def test_summary_header_once_and_rows(self):
        with tempfile.TemporaryDirectory() as d:
            logger.append_summary("TC-03", "PASS", sha="abc1234", root=d)
            logger.append_summary("TC-06", "FAIL", sha="abc1234", root=d)
            rows = open(os.path.join(d, "summary.csv")).read().splitlines()
            self.assertEqual(rows[0], "TC,sprint,date,sha,result")
            self.assertEqual(len(rows), 3)
            self.assertTrue(rows[1].startswith("TC-03,S2,") and rows[1].endswith(",abc1234,PASS"))

    def test_log_path(self):
        self.assertTrue(logger.log_path("TC-02", sha="1234567", root="r").replace("\\", "/").endswith("r/S2/TC-02_1234567.log"))

    def test_capture_splits_lines(self):
        class Fake:
            def __init__(self, *a, **k): self.data = [b"MARK CAND 5 state=2 fault=0\r\nMA", b"RK X 6\n", b""]
            def read(self, n): return self.data.pop(0) if self.data else b""
            def write(self, b): pass
            def close(self): pass
        with tempfile.TemporaryDirectory() as d:
            cap = logger.Capture("X", 115200, os.path.join(d, "t.log"), ["trials=5 waived"], serial_factory=Fake)
            self.assertIsNotNone(cap.wait_for(lambda l: l.startswith("MARK X"), 2))
            cap.close()
            txt = open(os.path.join(d, "t.log")).read()
            self.assertIn("# trials=5 waived", txt)
            self.assertIn("| MARK CAND 5 state=2 fault=0", txt)


class NodeBuild(unittest.TestCase):
    def test_build_opt_lines(self):
        import node_build as nb
        self.assertEqual(nb.build_opt_lines("foot", False), ["-DPPE_BUILD_NODE_TYPE=0"])
        self.assertEqual(nb.build_opt_lines("glove", True, closed_level=1),
                         ["-DPPE_BUILD_NODE_TYPE=1", "-DPPE_TEST_HOOKS", "-DPPE_CONTACT_CLOSED_LEVEL=1"])
        self.assertEqual(nb.build_opt_lines("helmet", True, adc_boot=800),
                         ["-DPPE_BUILD_NODE_TYPE=2", "-DPPE_TEST_HOOKS", "-DPPE_TEST_ADC_BOOT=800"])

    def test_dry_run_writes_nothing_into_the_sketch_folder(self):
        import node_build as nb, io, contextlib
        before = sorted(os.listdir(nb.SKETCH))
        with contextlib.redirect_stdout(io.StringIO()) as out:
            self.assertEqual(nb.main(["--type", "foot", "--hooks", "--dry-run"]), 0)
        self.assertIn("-DPPE_TEST_HOOKS", out.getvalue())
        self.assertEqual(sorted(os.listdir(nb.SKETCH)), before)


class WaitFor(unittest.TestCase):
    def test_only_lines_after_last_send_count(self):
        class Fake:
            def __init__(self, *a, **k): self.q = [b"STATE old\n"]
            def read(self, n): return self.q.pop(0) if self.q else b""
            def write(self, b): self.q.append(b"STATE new\n")
            def close(self): pass
        with tempfile.TemporaryDirectory() as d:
            cap = logger.Capture("X", 115200, os.path.join(d, "t.log"), serial_factory=Fake)
            time.sleep(0.4)                                  # the old line is now in the log
            cap.send("show")
            self.assertEqual(cap.wait_for(lambda l: l.startswith("STATE"), 2), "STATE new")
            cap.close()


if __name__ == "__main__":
    unittest.main()
