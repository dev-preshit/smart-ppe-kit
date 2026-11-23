import os, shutil, sys, tempfile, unittest
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import check_traceability as ct
DOCS = os.environ.get("PPE_DOCS") or os.path.join(os.path.dirname(__file__), "..", "..", "docs")
def have(): 
    try: [ct.find(DOCS, v) for v in ct.DOCS.values()]; return True
    except SystemExit: return False
@unittest.skipUnless(have(), "docs not found (set PPE_DOCS)")
class T(unittest.TestCase):
    def mutated(self, key, old, new, count=1):
        d = tempfile.mkdtemp()
        for v in ct.DOCS.values(): shutil.copy(ct.find(DOCS, v), os.path.join(d, os.path.basename(ct.find(DOCS, v))))
        p = ct.find(d, ct.DOCS[key]); s = open(p, encoding="utf-8").read(); self.assertIn(old, s)
        open(p, "w", encoding="utf-8").write(s.replace(old, new, count)); return d
    def run_(self, d, *extra):
        import io, contextlib
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf): rc = ct.main(["--docs", d, *extra])
        return rc, buf.getvalue()
    def test_real_docs_pass(self): self.assertEqual(self.run_(DOCS)[0], 0)
    def test_missing_test_id_in_traceability(self):
        rc, out = self.run_(self.mutated("trace", "| FR-12 | HMAC generation | TC-12 |", "| FR-12 | HMAC generation | - |")); self.assertEqual(rc, 1); self.assertIn("FR-12", out)
    def test_pass_line_edited(self):
        rc, out = self.run_(self.mutated("plan", "An independent Python verifier accepts 1000 of 1000 captured packets.", "A verifier accepts most packets.")); self.assertEqual(rc, 1); self.assertIn("TC-12", out)
    def test_pending_marker(self):
        rc, out = self.run_(self.mutated("sm", "## 0. How to read this document", "## 0. How to read this document (pending F-07)")); self.assertEqual(rc, 1); self.assertIn("pending", out)
    def test_old_document_name(self):
        rc, out = self.run_(self.mutated("plan", "docs/SRS_v1.2.md", "docs/SRS_v1.1.md")); self.assertEqual(rc, 1); self.assertIn("old document name", out)
    def test_transition_with_unknown_requirement(self):
        rc, out = self.run_(self.mutated("sm", "| FR-08, FR-11, FR-03 | TC-08, TC-11 |", "| FR-99, FR-11, FR-03 | TC-08, TC-11 |")); self.assertEqual(rc, 1); self.assertIn("FR-99", out)
    def test_transition_with_unknown_tc(self):
        rc, out = self.run_(self.mutated("sm", "| FR-17 | TC-17 |", "| FR-17 | TC-98 |")); self.assertEqual(rc, 1); self.assertIn("TC-98", out)
    def test_test_plan_section_missing_field(self):
        rc, out = self.run_(self.mutated("plan", "- **Tools:** SC-03, SC-13, EQ-02, SC-02.", "- **Tool:** SC-03.")); self.assertEqual(rc, 1); self.assertIn("TC-12 missing Tools", out)
    def test_sprint_summary_requires_rows(self):
        d = tempfile.mkdtemp(); rc, out = self.run_(DOCS, "--sprint", "S1", "--test-logs", d); self.assertEqual(rc, 1); self.assertIn("summary.csv", out)
    def _sprint_rows(self, sprint, tcs, fname):
        d = tempfile.mkdtemp()
        with open(os.path.join(d, fname), "w", newline="") as f:
            f.write("TC,sprint,date,sha,result\n")
            for t in tcs: f.write("%s,%s,2026-11-11,abc1234,PASS\n" % (t, sprint))
        return d
    S4_TCS = ["TC-01", "TC-07", "TC-08", "TC-09", "TC-10", "TC-11", "TC-13", "TC-14", "TC-15", "TC-17", "TC-75"]
    def test_s4_reads_its_own_summary_file(self):
        d = self._sprint_rows("S4", self.S4_TCS, "summary_s4.csv"); rc, out = self.run_(DOCS, "--sprint", "S4", "--test-logs", d); self.assertEqual(rc, 0, out)
    def test_s4_missing_row_fails_and_names_its_file(self):
        d = self._sprint_rows("S4", self.S4_TCS[:-1], "summary_s4.csv"); rc, out = self.run_(DOCS, "--sprint", "S4", "--test-logs", d); self.assertEqual(rc, 1); self.assertIn("summary_s4.csv has no S4 row for TC-75", out)
    def test_s4_rows_in_the_shared_file_are_still_found_when_no_own_file_exists(self):
        d = self._sprint_rows("S4", self.S4_TCS, "summary.csv"); rc, out = self.run_(DOCS, "--sprint", "S4", "--test-logs", d); self.assertEqual(rc, 0, out)
    def test_own_file_wins_over_the_shared_file(self):
        d = self._sprint_rows("S4", self.S4_TCS, "summary.csv"); open(os.path.join(d, "summary_s4.csv"), "w").write("TC,sprint,date,sha,result\n")
        rc, out = self.run_(DOCS, "--sprint", "S4", "--test-logs", d); self.assertEqual(rc, 1); self.assertIn("summary_s4.csv has no S4 row for TC-01", out)
if __name__ == "__main__": unittest.main()
