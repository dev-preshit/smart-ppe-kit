"""Unit tests of tools/s5_host_evidence.py (no make, no hardware)."""
import argparse, os, sys, tempfile, unittest
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import s5_host_evidence as h


def out(n26=7, n28=11, fail26=False, failed=0, summary=True):
    L = ["release_check: no hook symbols"]
    for i in range(n26):
        L.append("TEST\tTC-26\tt%d\t%s\tchecks=5" % (i, "FAIL" if fail26 and i == 0 else "PASS"))
    for i in range(n28):
        L.append("TEST\tTC-28\tu%d\tPASS\tchecks=5" % i)
    L.append("TEST\tTC-260\tother\tPASS\tchecks=1")
    if summary:
        L.append("SUMMARY\ttests=%d\tfailed=%d\tchecks=99" % (n26 + n28 + 1, failed))
    return "\n".join(L) + "\n"


class Analyze(unittest.TestCase):
    def test_pass(self):
        self.assertEqual(h.analyze_output(out(), "TC-26")[0], "PASS")
        self.assertEqual(h.analyze_output(out(), "TC-28")[0], "PASS")

    def test_tag_must_match_exactly(self):
        self.assertEqual(len(h.tagged_lines(out(), "TC-26")), 7)          # TC-260 is not TC-26

    def test_one_failing_test_fails(self):
        self.assertEqual(h.analyze_output(out(fail26=True, failed=1), "TC-26")[0], "FAIL")

    def test_failure_elsewhere_in_the_suite_fails(self):
        self.assertEqual(h.analyze_output(out(failed=1), "TC-26")[0], "FAIL")

    def test_missing_tests_fail(self):
        self.assertEqual(h.analyze_output(out(n26=6), "TC-26")[0], "FAIL")
        self.assertEqual(h.analyze_output(out(n28=10), "TC-28")[0], "FAIL")

    def test_make_error_exit_fails_and_no_summary_is_blocked(self):
        self.assertEqual(h.analyze_output(out(), "TC-26", make_rc=2)[0], "FAIL")
        self.assertEqual(h.analyze_output(out(summary=False), "TC-26")[0], "BLOCKED")


class Result(unittest.TestCase):
    def test_result_flow(self):
        with tempfile.TemporaryDirectory() as root:
            import logger
            path = logger.log_path("TC-26", sprint="S5", sha=logger.git_sha7(), root=os.path.join(root, "test_logs"))
            os.makedirs(os.path.dirname(path))
            a = argparse.Namespace(tc="TC-26", result="PASS", note="n")
            with self.assertRaises(SystemExit):
                h.cmd_result(a, root=root)                                 # no log
            body = "# tc=TC-26\n" + out() + "VERDICT FAIL x\n"
            with open(path, "w") as f: f.write(body)
            with self.assertRaises(SystemExit):
                h.cmd_result(a, root=root)                                 # verdict says FAIL
            with open(path, "w") as f: f.write("# tc=TC-26\n" + out(fail26=True, failed=1) + "VERDICT PASS forged\n")
            with self.assertRaises(SystemExit):
                h.cmd_result(a, root=root)                                 # analyzer disagrees
            with open(path, "w") as f: f.write("# tc=TC-26\n" + out() + "VERDICT PASS ok\n")
            self.assertEqual(h.cmd_result(a, root=root), 0)
            with open(os.path.join(root, "test_logs", "summary_s5.csv")) as f:
                rows = f.read().strip().splitlines()
            self.assertEqual(rows[0], "TC,sprint,date,sha,result")
            self.assertTrue(rows[1].startswith("TC-26,S5,") and rows[1].endswith(",PASS"))


if __name__ == "__main__":
    unittest.main()
