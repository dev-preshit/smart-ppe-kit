import os, sys, tempfile, unittest
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import observe

def adv(node=3, flags=0x20, state=1, batt=100, ctr=258):
    b = bytearray([0x14, 0xFF, 0xFF, 0xFF, flags, 1, node, state, batt]) + ctr.to_bytes(4, "little") + bytes(8)
    return b.hex()

def make_log(copies=5, spacing_ms=500, t0_s=20.0, pdus=5, pdu_ms=20, evt=3, lost=None, end_after_s=12.0,
             tweak=None, hexes=None, ambient=True):
    """A synthetic observer log. tweak(k, start_us) may move the start of copy k."""
    lines = ["# tc=TEST", "2026-11-11T10:00:00.000 | OBSERVER ready scan=1 interval=100 ms window=100 ms"]
    hexes = hexes or [adv()]
    last = 0
    for h in hexes:
        for k in range(copies):
            s = int(t0_s * 1e6 + k * spacing_ms * 1000)
            if tweak: s = tweak(k, s)
            for j in range(pdus):
                us = s + j * pdu_ms * 1000
                last = max(last, us)
                lines.append("2026-11-11T10:00:01.000 | PKT %d.%06d 21 %d -60 %s" % (us // 1000000, us % 1000000, evt, h))
        t0_s += 30
    if ambient:
        lines.append("2026-11-11T10:00:01.000 | PKT 21.100000 23 0 -90 02010604ff3030300e0941505f31353433383538313237")
    endus = last + int(end_after_s * 1e6)
    lines.append("2026-11-11T10:00:30.000 | TIME %d.%06d" % (endus // 1000000, endus % 1000000))
    lines.append("2026-11-11T10:00:30.500 | STAT seen=99 printed=99 lost=%d filter=0" % (lost or 0))
    d = tempfile.mkdtemp(); p = os.path.join(d, "TEST.log")
    with open(p, "w", encoding="utf-8") as f: f.write("\n".join(lines) + "\n")
    return p

class A:
    def __init__(self, log, **kw):
        self.tc = "TEST"; self.log = log; self.len = 21; self.prefix = "14ffffff"; self.gap = 0.2
        self.spacing = 500; self.copies = 5; self.tol = 50; self.quiet = 10.0; self.expect_tx = None
        self.__dict__.update(kw)

class T(unittest.TestCase):
    def run_(self, log, **kw):
        import io, contextlib
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf): rc = observe.cmd_analyze(A(log, **kw))
        return rc, buf.getvalue()
    def test_five_copies_pass(self):
        rc, out = self.run_(make_log()); self.assertEqual(rc, 0, out); self.assertIn("copies=5", out); self.assertIn("ctr=258", out)
    def test_four_copies_fail(self):
        rc, out = self.run_(make_log(copies=4)); self.assertEqual(rc, 1); self.assertIn("4 copies, expected 5", out)
    def test_six_copies_fail(self):
        rc, out = self.run_(make_log(copies=6)); self.assertEqual(rc, 1); self.assertIn("6 copies, expected 5", out)
    def test_spacing_outside_tolerance_fails(self):
        rc, out = self.run_(make_log(tweak=lambda k, s: s + (60000 if k == 3 else 0))); self.assertEqual(rc, 1); self.assertIn("limit 500 +/- 50", out)
    def test_spacing_inside_tolerance_passes(self):
        rc, out = self.run_(make_log(tweak=lambda k, s: s + (30000 if k == 2 else 0))); self.assertEqual(rc, 0, out)
    def test_capture_too_short_fails(self):
        rc, out = self.run_(make_log(end_after_s=3.0)); self.assertEqual(rc, 1); self.assertIn("need 10.0 s of observed silence", out)
    def test_lost_packets_fail(self):
        rc, out = self.run_(make_log(lost=2)); self.assertEqual(rc, 1); self.assertIn("lost=2", out)
    def test_wrong_pdu_type_fails(self):
        rc, out = self.run_(make_log(evt=2)); self.assertEqual(rc, 1); self.assertIn("not ADV_NONCONN_IND", out)
    def test_no_transmission_fails(self):
        rc, out = self.run_(make_log(copies=0)); self.assertEqual(rc, 1); self.assertIn("no matching transmission", out)
    def test_expect_tx_counts_transmissions(self):
        log = make_log(hexes=[adv(ctr=1), adv(ctr=2)])
        self.assertEqual(self.run_(log, expect_tx=2)[0], 0)
        self.assertEqual(self.run_(log, expect_tx=1)[0], 1)
    def test_two_transmissions_report_the_interval(self):
        rc, out = self.run_(make_log(hexes=[adv(flags=0x00, ctr=1), adv(flags=0x00, ctr=2)])); self.assertEqual(rc, 0, out); self.assertIn("first copy 30.000 s after", out); self.assertIn("type=heartbeat", out)
    def test_analysis_is_appended_to_the_log(self):
        p = make_log(); self.run_(p); self.assertIn("# ANALYSIS RESULT PASS", open(p).read())
    def test_calibrate_reports_error(self):
        import io, contextlib
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf): rc = observe.cmd_calibrate(A(make_log(copies=20, tweak=lambda k, s: s + (7000 if k % 2 else 0))))
        self.assertEqual(rc, 0); self.assertIn("OBSERVER_ERROR_MS max=7.0", buf.getvalue())
    def test_packets_before_reset_are_ignored(self):
        p = make_log(); body = open(p).read().split("\n")
        junk = ["2026-11-11T10:00:00.500 | PKT 5.000000 21 3 -60 " + adv(ctr=99), "2026-11-11T10:00:00.600 | OK reset"]
        open(p, "w").write("\n".join(body[:2] + junk + body[2:]))
        rc, out = self.run_(p); self.assertEqual(rc, 0, out); self.assertNotIn("ctr=99", out)
    def test_calibrate_fails_when_packets_were_lost(self):
        import io, contextlib
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf): rc = observe.cmd_calibrate(A(make_log(copies=20, lost=3)))
        self.assertEqual(rc, 1); self.assertIn("lost=3: these numbers are not valid", buf.getvalue())
if __name__ == "__main__": unittest.main()
