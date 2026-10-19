"""Tests for tools/inject.py, tools/s3_capture.py and tools/gateway_stub.py (S3). No hardware, no serial port."""
import csv
import json
import os
import sys
import tempfile
import threading
import unittest
import urllib.request

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import gen_vectors as gv
import inject as inj
import s3_capture as cap
import gateway_stub as gws


class ModelHub:
    """Python model of the hub counters, written from the spec (section 2 order, section 6.2 counter rule)."""
    def __init__(self, mutate=False):
        self.g = {k: 0 for k in inj.GCOUNTERS}
        self.n = {i: dict(state="UNKNOWN", last_counter=0, last_seen_ms=0, accepted=0, dup=0, replay=0, gap=0, hmac_fail=0)
                  for i in range(1, 6)}
        self.mutate = mutate

    def feed(self, data):
        self.g["rx_total"] += 1
        if len(data) != 21 or data[:4] != bytes([0x14, 0xFF, 0xFF, 0xFF]):
            self.g["drop_len"] += 1
            return
        p = data[4:]
        st = gv.expected_stage(p)
        if st == gv.DROP_VER:
            self.g["drop_vest" if self.mutate else "drop_ver"] += 1
        elif st == gv.DROP_VEST:
            self.g["drop_vest"] += 1
        elif st == gv.DROP_NODE:
            self.g["drop_node"] += 1
        elif st == gv.HMAC_FAIL:
            self.n[p[2]]["hmac_fail"] += 1
        elif st == gv.DROP_STATE:
            self.g["drop_state"] += 1
        else:
            n = self.n[p[2]]
            c = int.from_bytes(p[5:9], "little")
            if c > n["last_counter"]:
                n["last_counter"] = c
                n["accepted"] += 1
            elif c == n["last_counter"]:
                n["dup"] += 1
            else:
                n["replay"] += 1

    def dump_lines(self):
        lines = ["DUMP begin t_ms=1", "DUMP global " + " ".join("%s=%d" % (k, v) for k, v in self.g.items()) + " scan_quiet_60s=0"]
        for i, n in self.n.items():
            lines.append("DUMP node=%d state=%s last_counter=%d last_seen_ms=%d accepted=%d dup=%d replay=%d gap=%d hmac_fail=%d battery=90 flags=0x60"
                         % (i, n["state"], n["last_counter"], n["last_seen_ms"], n["accepted"], n["dup"], n["replay"], n["gap"], n["hmac_fail"]))
        lines.append("DUMP end")
        return lines


class FakeLink:
    def __init__(self, model):
        self.m = model

    def send(self, data):
        self.m.feed(data)
        return True

    def snapshot(self):
        return inj.parse_dump(self.m.dump_lines())

    def settle(self, s=0):
        pass


class Null:
    def packet(self, *a):
        pass

    def note(self, *a):
        pass


class InjectTests(unittest.TestCase):
    def test_flip_set_is_136_all_rejected_with_spec_stage(self):
        c = inj.flip_cases()
        self.assertEqual(len(c), 136)
        self.assertTrue(all(st != gv.OK for _, _, st in c))
        self.assertEqual(sum(1 for _, _, st in c if st == gv.DROP_VER), 2)   # only flips of version bits 6 and 7 (spec: checked before the HMAC)

    def test_run_set_passes_against_a_correct_model(self):
        m = ModelHub()
        fails = inj.run_set(FakeLink(m), Null(), inj.defect_cases(300, malformed=True), True)
        self.assertEqual(fails, 0)

    def test_run_set_detects_a_wrong_counter(self):
        m = ModelHub(mutate=True)                     # counts drop_ver as drop_vest
        fails = inj.run_set(FakeLink(m), Null(), inj.defect_cases(300), True)
        self.assertGreater(fails, 0)

    def test_flips_pass_after_a1_is_accepted_and_last_counter_is_unchanged(self):
        m = ModelHub()
        link = FakeLink(m)
        a1 = gv.adv(gv.self_check()[0])
        self.assertEqual(inj.run_set(link, Null(), [("A1", a1, gv.OK)], True), 0)
        self.assertEqual(m.n[3]["last_counter"], 258)
        self.assertEqual(inj.run_set(link, Null(), inj.flip_cases(), True), 0)
        self.assertEqual(m.n[3]["last_counter"], 258)           # TC-23: last_counter unchanged by the 136 variants
        self.assertEqual(m.n[3]["accepted"], 1)

    def test_defect_expectations_match_the_independent_spec_order(self):
        for label, data, stage in inj.defect_cases(300, malformed=True):
            if len(data) == 21 and data[:4] == bytes([0x14, 0xFF, 0xFF, 0xFF]):
                self.assertEqual(gv.expected_stage(data[4:]), stage, label)
            else:
                self.assertEqual(stage, gv.DROP_LEN, label)

    def test_defect_set_has_the_required_cases(self):
        names = [n for n, _, _ in inj.defect_cases(300, malformed=True)]
        for want in ("len20", "len22", "len0", "hdr_adlen", "hdr_adtype", "hdr_cid_lo", "hdr_cid_hi", "ver0", "ver2", "ver3",
                     "wrong_vest", "node0", "node6", "node255", "bad_hmac", "state02_valid_hmac", "valid_control"):
            self.assertIn(want, names)
        without = [n for n, _, _ in inj.defect_cases(300)]
        self.assertNotIn("len20", without)                      # malformed ones only on request

    def test_forged_set_all_fail_hmac(self):
        c = inj.forged_cases(50)
        self.assertEqual(len(c), 50)
        for _, data, st in c:
            self.assertEqual(gv.expected_stage(data[4:]), gv.HMAC_FAIL)
            self.assertEqual(len(data), 21)

    def test_signed_packet_matches_appendix_a1(self):
        self.assertEqual(inj.signed(3, 258), gv.adv(gv.self_check()[0]))

    def test_parse_dump_and_diff(self):
        m = ModelHub()
        before = inj.parse_dump(m.dump_lines())
        m.feed(inj.signed(2, 5))
        d = inj.diff(before, inj.parse_dump(m.dump_lines()))
        self.assertEqual(d, {"g.rx_total": 1, "n2.accepted": 1})


class CaptureTests(unittest.TestCase):
    def test_log_path_and_tc_validation(self):
        self.assertEqual(cap.log_path("/r", "TC-24", "abc1234"), os.path.join("/r", "test_logs", "S3", "TC-24_abc1234.log"))
        with self.assertRaises(ValueError):
            cap.log_path("/r", "tc24", "abc1234")

    def test_summary_row_columns_and_append_only(self):
        d = tempfile.mkdtemp()
        cap.append_summary(d, "TC-20", "PASS", "abc1234", "2026-10-19T10:00:00+05:30")
        cap.append_summary(d, "TC-21", "FAIL", "abc1234", "2026-10-19T10:05:00+05:30")
        rows = list(csv.reader(open(os.path.join(d, "test_logs", "summary.csv"))))
        self.assertEqual(rows[0], ["TC", "sprint", "date", "sha", "result"])
        self.assertEqual(rows[1], ["TC-20", "S3", "2026-10-19T10:00:00+05:30", "abc1234", "PASS"])
        self.assertEqual(len(rows), 3)
        with self.assertRaises(ValueError):
            cap.append_summary(d, "TC-22", "MAYBE", "abc1234")

    def test_summary_appends_to_an_existing_s1_file_without_a_second_header(self):
        d = tempfile.mkdtemp()
        os.makedirs(os.path.join(d, "test_logs"))
        with open(os.path.join(d, "test_logs", "summary.csv"), "w", newline="") as f:
            f.write("TC,sprint,date,sha,result\nTC-12,S1,2026-10-16T14:42:56+05:30,a954e48,PASS\n")
        cap.append_summary(d, "TC-23", "PASS", "abc1234")
        lines = open(os.path.join(d, "test_logs", "summary.csv")).read().splitlines()
        self.assertEqual(sum(1 for l in lines if l.startswith("TC,sprint")), 1)
        self.assertEqual(len(lines), 3)

    @staticmethod
    def dump(acc, gap=0, ctr=9):
        return ("t,hub,DUMP node=5 state=WORN last_counter=%d last_seen_ms=1 accepted=%d dup=0 replay=0 gap=%d hmac_fail=0\n"
                % (ctr, acc, gap))

    def test_tc20_pass_rule_single_dump(self):
        d = self.dump
        self.assertEqual(cap.analyze_tc20(d(120), 5, 120)[0], "PASS")
        self.assertEqual(cap.analyze_tc20(d(119), 5, 120)[0], "PASS")     # 1 miss allowed (99.17 %)
        self.assertEqual(cap.analyze_tc20(d(118), 5, 120)[0], "FAIL")     # 2 misses
        self.assertEqual(cap.analyze_tc20("nothing", 5, 120)[0], "BLOCKED")

    def test_tc20_uses_the_delta_inside_the_latest_run(self):
        d = self.dump
        old = "# RUN old\n" + d(10) + d(50)
        new = "# RUN new\n" + d(50) + d(169)                                 # delta 119
        self.assertEqual(cap.analyze_tc20(old + new, 5, 120)[0], "PASS")
        self.assertIn("accepted 119 of 120", cap.analyze_tc20(old + new, 5, 120)[1])
        self.assertEqual(cap.analyze_tc20("# RUN x\n" + d(50) + d(167), 5, 120)[0], "FAIL")   # delta 117

    def test_wifi_recheck_requires_gaps_equal_misses(self):
        d = self.dump
        ok = "# RUN w\n" + d(10, gap=0) + d(129, gap=1)                      # 119 accepted, 1 miss, 1 gap
        self.assertEqual(cap.analyze_tc20(ok, 5, 120, recheck=True)[0], "PASS")
        bad = "# RUN w\n" + d(10, gap=0) + d(129, gap=3)                     # gaps that no missing packet explains
        self.assertEqual(cap.analyze_tc20(bad, 5, 120, recheck=True)[0], "FAIL")
        self.assertEqual(cap.analyze_tc20(bad, 5, 120, recheck=False)[0], "PASS")   # plain TC-20 ignores gap_count
        clean = "# RUN w\n" + d(10) + d(130)
        self.assertEqual(cap.analyze_tc20(clean, 5, 120, recheck=True)[0], "PASS")

    def test_tc72_pass_rule(self):
        def log(count, mx, over, qfull):
            return ("DUMP queue fill=0 depth=64 max_fill=9 queue_full=%d callbacks_seen=%d\n"
                    "DUMP proc count=%d last_us=300 max_us=%d avg_us=250 over_10ms=%d\n"
                    "DUMP persist pending=0 writes=40 write_fails=0 dropped=0 last_us=900 max_us=20000 avg_us=1500\n"
                    % (qfull, count, count, mx, over))
        self.assertEqual(cap.analyze_tc72(log(1000, 9999, 0, 0))[0], "PASS")
        self.assertEqual(cap.analyze_tc72(log(1000, 10001, 0, 0))[0], "FAIL")
        self.assertEqual(cap.analyze_tc72(log(999, 500, 0, 0))[0], "FAIL")
        self.assertEqual(cap.analyze_tc72(log(1025, 500, 1, 0))[0], "FAIL")
        self.assertEqual(cap.analyze_tc72(log(1025, 500, 0, 3))[0], "FAIL")
        self.assertEqual(cap.analyze_tc72("")[0], "BLOCKED")
        self.assertIn("NVS writes 40", cap.analyze_tc72(log(1000, 500, 0, 0))[1])


class GatewayStubTests(unittest.TestCase):
    def test_get_and_post_are_logged_and_answered(self):
        d = tempfile.mkdtemp()
        logf = os.path.join(d, "arrivals.log")
        srv = gws.make_server("127.0.0.1", 0, logf)
        port = srv.server_address[1]
        t = threading.Thread(target=srv.serve_forever, daemon=True)
        t.start()
        try:
            r = urllib.request.urlopen("http://127.0.0.1:%d/ping?x=1" % port, timeout=5)
            self.assertEqual(json.loads(r.read()), {"ok": True})
            req = urllib.request.Request("http://127.0.0.1:%d/hub/status" % port, data=b'{"a":1}', method="POST")
            self.assertEqual(urllib.request.urlopen(req, timeout=5).status, 200)
        finally:
            srv.shutdown()
        recs = [json.loads(l) for l in open(logf)]
        self.assertEqual([r["method"] for r in recs], ["GET", "POST"])
        self.assertEqual(recs[1]["path"], "/hub/status")
        self.assertEqual(recs[1]["bytes"], 7)
        self.assertIn("arrival", recs[0])


if __name__ == "__main__":
    unittest.main()
