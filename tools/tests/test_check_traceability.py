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
if __name__ == "__main__": unittest.main()
