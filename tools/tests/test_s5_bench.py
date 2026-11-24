"""Unit tests of tools/s5_bench.py analyzers and result recording (no hardware, no pyserial needed)."""
import os, sys, tempfile, unittest
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import s5_bench as b


def acc(t, node, ctr, prev="OFFLINE", now="WORN"):
    return "ACC t_ms=%d t_us=%d node=%d ctr=%d state=1 batt=90 flags=0x60 missed=0 prev=%s now=%s" % (t, t * 1000, node, ctr, prev, now)


def off(t, node, age):
    return "MSG msg_type=NODE_OFFLINE vest_id=1 node_id=%d state=OFFLINE last_seen_age_s=%d t_ms=%d" % (node, age, t)


def dump(t, states, alert=0, vest="NON_COMPLIANT", hb=360, margin=60, seen=None):
    out = ["DUMP begin t_ms=%d" % t,
           "DUMP hub vest=0x01 accept_enabled=1 registry_ok=1 vest_state=%s alert_active=%d heartbeat_s=%d margin_s=%d offline_timeout_s=%d msg_pending=0 msg_dropped=0"
           % (vest, alert, hb, margin, 2 * hb + margin)]
    for i, s in enumerate(states, 1):
        out.append("DUMP node=%d state=%s last_counter=%d last_seen_ms=%d accepted=1 dup=0 replay=0 gap=0 hmac_fail=0 battery=90 flags=0x60"
                   % (i, s, 100 * i, (seen or {}).get(i, 1000)))
    out.append("DUMP end")
    return out


def put(path, mode, text):
    with open(path, mode) as f:
        f.write(text)


def log(lines, pc=True, skew_ms=0):
    """PC prefixes follow the hub t_ms of each line (lines without t_ms keep the last time), so the PC and hub clocks
    agree unless skew_ms moves the NODE_OFFLINE lines."""
    import datetime, re
    base = datetime.datetime(2026, 11, 14, 10, 0, 0)
    out, cur = [], 0
    for l in lines:
        m = re.search(r"t_ms=(\d+)", l)
        if m:
            cur = int(m.group(1))
        extra = skew_ms if "NODE_OFFLINE" in l else 0
        stamp = (base + datetime.timedelta(milliseconds=cur + extra)).isoformat(timespec="milliseconds")
        out.append("%s | %s" % (stamp, l) if pc else l)
    return "\n".join(out)


class Offline(unittest.TestCase):
    def run_case(self, delta_ms, age_s=None, extra=None):
        t0 = 5000000
        lines = [acc(t0, 2, 1100)] + (extra or []) + [off(t0 + delta_ms, 2, age_s if age_s is not None else delta_ms // 1000)]
        return b.analyze_offline(log(lines), 2, 1100, 780)[0]

    def test_window_edges(self):
        self.assertEqual(self.run_case(779999), "FAIL")
        self.assertEqual(self.run_case(780000), "PASS")
        self.assertEqual(self.run_case(780950), "PASS")
        self.assertEqual(self.run_case(785000), "PASS")
        self.assertEqual(self.run_case(785001), "FAIL")

    def test_margin_zero_window(self):
        t0 = 100
        self.assertEqual(b.analyze_offline(log([acc(t0, 2, 7), off(t0 + 721000, 2, 721)]), 2, 7, 720)[0], "PASS")
        self.assertEqual(b.analyze_offline(log([acc(t0, 2, 7), off(t0 + 726000, 2, 726)]), 2, 7, 720)[0], "FAIL")

    def test_wrap_of_the_32_bit_clock(self):
        t0 = 0xFFFFFFFF - 100000
        self.assertEqual(b.analyze_offline(log([acc(t0, 2, 7), off((t0 + 781000) & 0xFFFFFFFF, 2, 781)], pc=False), 2, 7, 780)[0], "PASS")

    def test_no_offline_is_a_fail_not_a_pass(self):
        self.assertEqual(b.analyze_offline(log([acc(10, 2, 7)]), 2, 7, 780)[0], "FAIL")

    def test_missing_packet_is_blocked(self):
        self.assertEqual(b.analyze_offline(log([off(900000, 2, 780)]), 2, 7, 780)[0], "BLOCKED")

    def test_node_heard_again_is_blocked(self):
        self.assertEqual(self.run_case(781000, extra=[acc(1000000 + 5000000, 2, 1101, "WORN")]), "BLOCKED")

    def test_age_field_must_agree(self):
        self.assertEqual(self.run_case(781000, age_s=700), "FAIL")

    def test_other_nodes_offline_lines_do_not_count(self):
        t0 = 5000
        self.assertEqual(b.analyze_offline(log([acc(t0, 2, 7), off(t0 + 780500, 1, 780), off(t0 + 781000, 2, 781)]), 2, 7, 780)[0], "PASS")
        self.assertEqual(b.analyze_offline(log([acc(t0, 2, 7), off(t0 + 780500, 1, 780)]), 2, 7, 780)[0], "FAIL")

    def test_pc_clock_disagreeing_with_the_hub_clock_fails(self):
        t0 = 5000
        good = [acc(t0, 2, 7), off(t0 + 781000, 2, 781)]
        self.assertEqual(b.analyze_offline(log(good, skew_ms=1900), 2, 7, 780)[0], "PASS")
        v, m = b.analyze_offline(log(good, skew_ms=2100), 2, 7, 780)
        self.assertEqual(v, "FAIL"); self.assertIn("PC time", m)
        self.assertEqual(b.analyze_offline(log(good, skew_ms=-2100), 2, 7, 780)[0], "FAIL")

    def test_raw_lines_without_pc_prefix(self):
        self.assertEqual(b.analyze_offline(log([acc(5, 2, 7), off(5 + 781000, 2, 781)], pc=False), 2, 7, 780)[0], "PASS")


class Drop1(unittest.TestCase):
    def test_pass(self):
        t = log([acc(1000, 2, 5), acc(1000 + 720000, 2, 6, "WORN")])
        self.assertEqual(b.analyze_drop1(t, 2, 5, 6, 360, 780)[0], "PASS")

    def test_offline_between_fails(self):
        t = log([acc(1000, 2, 5), off(1000 + 781000, 2, 781), acc(1000 + 800000, 2, 6, "OFFLINE")])
        self.assertEqual(b.analyze_drop1(t, 2, 5, 6, 360, 780)[0], "BLOCKED")        # gap 800 s is above the timeout: meaningless
        t = log([acc(1000, 2, 5), off(1000 + 500000, 2, 500), acc(1000 + 720000, 2, 6, "OFFLINE")])
        self.assertEqual(b.analyze_drop1(t, 2, 5, 6, 360, 780)[0], "FAIL")

    def test_wrong_gap_is_blocked(self):
        t = log([acc(1000, 2, 5), acc(1000 + 400000, 2, 6, "WORN")])
        self.assertEqual(b.analyze_drop1(t, 2, 5, 6, 360, 780)[0], "BLOCKED")

    def test_pc_gap_disagreeing_fails(self):
        import re
        good = log([acc(1000, 2, 5), acc(1000 + 720000, 2, 6, "WORN")]).splitlines()
        self.assertEqual(b.analyze_drop1("\n".join(good), 2, 5, 6, 360, 780)[0], "PASS")
        bad = good[:1] + [re.sub(r"T10:12:", "T10:15:", good[1])]
        self.assertEqual(b.analyze_drop1("\n".join(bad), 2, 5, 6, 360, 780)[0], "FAIL")

    def test_second_packet_not_worn_fails(self):
        t = log([acc(1000, 2, 5), acc(1000 + 720000, 2, 6, "NOT_WORN")])
        self.assertEqual(b.analyze_drop1(t, 2, 5, 6, 360, 780)[0], "FAIL")

    def test_missing_second_packet_is_blocked(self):
        self.assertEqual(b.analyze_drop1(log([acc(1000, 2, 5)]), 2, 5, 6, 360, 780)[0], "BLOCKED")


def boot_log(unknown=True, alert=0, comp=False, off5=781000, other_off=False, primed=True, boots=1, prefix_msg=False, last_alert=1):
    L = dump(100, ["WORN"] * 5 if primed else ["UNKNOWN"] * 5, vest="COMPLIANT")
    if prefix_msg:
        L += ["BOOT reset_reason=1"] + [off(150, 3, 10)]
    else:
        L += ["HUB boot (S3 receive path)", "BOOT reset_reason=1"]
    L += ["MSG msg_type=HUB_BOOT vest_id=1 reset_reason=1 fw=s5-hub-logic t_ms=341"]
    if boots == 2:
        L += ["BOOT reset_reason=1", "MSG msg_type=HUB_BOOT vest_id=1 reset_reason=1 fw=s5-hub-logic t_ms=9"]
    st = ["UNKNOWN"] * 5 if unknown else ["UNKNOWN", "WORN", "UNKNOWN", "UNKNOWN", "UNKNOWN"]
    L += dump(3400, st, alert=alert, seen={i: 341 for i in range(1, 6)})
    if comp:
        L += ["MSG msg_type=COMPLIANCE vest_id=1 state=COMPLIANT nodes_not_ok=[] t_ms=500000"]
    if other_off:
        L += [off(781341, 1, 781)]
    L += [off(341 + off5, 5, off5 // 1000)]
    L += dump(900000, ["WORN"] * 4 + ["OFFLINE"], alert=last_alert)
    return log(L)


class Boot(unittest.TestCase):
    def test_pass(self):
        v, m = b.analyze_boot(boot_log())
        self.assertEqual(v, "PASS", m)

    def test_window_edges(self):
        self.assertEqual(b.analyze_boot(boot_log(off5=779900))[0], "FAIL")
        self.assertEqual(b.analyze_boot(boot_log(off5=785100))[0], "FAIL")
        self.assertEqual(b.analyze_boot(boot_log(off5=785000))[0], "PASS")

    def test_not_unknown_after_reset_fails(self):
        self.assertEqual(b.analyze_boot(boot_log(unknown=False))[0], "FAIL")

    def test_alert_after_reset_fails(self):
        self.assertEqual(b.analyze_boot(boot_log(alert=1))[0], "FAIL")

    def test_compliance_message_fails(self):
        self.assertEqual(b.analyze_boot(boot_log(comp=True))[0], "FAIL")

    def test_other_node_offline_fails(self):
        self.assertEqual(b.analyze_boot(boot_log(other_off=True))[0], "FAIL")

    def test_unprimed_hub_is_blocked(self):
        self.assertEqual(b.analyze_boot(boot_log(primed=False))[0], "BLOCKED")

    def test_two_boots_blocked(self):
        self.assertEqual(b.analyze_boot(boot_log(boots=2))[0], "BLOCKED")

    def test_message_before_hub_boot_fails(self):
        self.assertEqual(b.analyze_boot(boot_log(prefix_msg=True))[0], "FAIL")

    def test_pc_clock_disagreeing_fails(self):
        import re
        text = boot_log()
        lines = text.splitlines()
        i = [k for k, l in enumerate(lines) if "NODE_OFFLINE" in l][0]
        lines[i] = re.sub(r"T10:1[0-9]:\d\d", "T10:40:00", lines[i])
        self.assertEqual(b.analyze_boot("\n".join(lines))[0], "FAIL")

    def test_no_alert_at_the_end_fails(self):
        self.assertEqual(b.analyze_boot(boot_log(last_alert=0))[0], "FAIL")

    def test_no_hub_boot_is_blocked(self):
        self.assertEqual(b.analyze_boot(log(dump(1, ["WORN"] * 5)))[0], "BLOCKED")


class Result(unittest.TestCase):
    def test_result_requires_a_matching_pass_verdict(self):
        import argparse, logger
        with tempfile.TemporaryDirectory() as root:
            sha = b.head_sha()
            path = logger.log_path("TC-27a", sprint="S5", sha=sha, root=os.path.join(root, "test_logs"))
            os.makedirs(os.path.dirname(path))
            body = ["# PARAM"]
            t0 = 5000
            body = [("2026-11-14T10:00:00.000 | PARAM case=offline node=2 ctr=7 timeout_s=780"),
                    ("2026-11-14T10:00:01.000 | " + acc(t0, 2, 7)), ("2026-11-14T10:13:01.000 | " + off(t0 + 781000, 2, 781))]
            put(path, "w", "\n".join(body) + "\n")
            a = argparse.Namespace(tc="TC-27a", result="PASS", note="x")
            with self.assertRaises(SystemExit):                  # no VERDICT line yet
                b.cmd_result(a, root=root)
            put(path, "a", "2026-11-14T10:13:02.000 | VERDICT FAIL test\n")
            with self.assertRaises(SystemExit):                  # driver said FAIL
                b.cmd_result(a, root=root)
            put(path, "a", "2026-11-14T10:13:03.000 | VERDICT PASS ok\n")
            self.assertEqual(b.cmd_result(a, root=root), 0)
            with open(os.path.join(root, "test_logs", "summary_s5.csv")) as f:
                rows = f.read().strip().splitlines()
            self.assertEqual(rows[0], "TC,sprint,date,sha,result")
            self.assertTrue(rows[1].startswith("TC-27a,S5,") and rows[1].endswith(",PASS"))

    def test_result_refuses_when_the_analyzer_disagrees(self):
        import argparse, logger
        with tempfile.TemporaryDirectory() as root:
            path = logger.log_path("TC-27a", sprint="S5", sha=b.head_sha(), root=os.path.join(root, "test_logs"))
            os.makedirs(os.path.dirname(path))
            body = ["PARAM case=offline node=2 ctr=7 timeout_s=780", acc(5000, 2, 7), off(5000 + 790000, 2, 790), "VERDICT PASS forged"]
            put(path, "w", "\n".join(body) + "\n")
            with self.assertRaises(SystemExit):
                b.cmd_result(argparse.Namespace(tc="TC-27a", result="PASS", note=""), root=root)
            self.assertFalse(os.path.exists(os.path.join(root, "test_logs", "summary_s5.csv")))


REAL = [  # lines copied from the hub rehearsal of 2026-11-14 (firmware s5-hub-logic)
    "ACC t_ms=2156469 t_us=2156470089 node=1 ctr=1000 state=1 batt=90 flags=0x60 missed=999 prev=OFFLINE now=WORN",
    "MSG msg_type=COMPLIANCE vest_id=1 state=COMPLIANT nodes_not_ok=[] t_ms=2159851",
    "MSG msg_type=COMPLIANCE vest_id=1 state=NON_COMPLIANT nodes_not_ok=[2] t_ms=2160704",
    "MSG msg_type=HUB_BOOT vest_id=1 reset_reason=1 fw=s5-hub-logic t_ms=341",
    "DUMP hub vest=0x01 accept_enabled=1 registry_ok=1 vest_state=COMPLIANT alert_active=0 heartbeat_s=360 margin_s=60 offline_timeout_s=780 msg_pending=0 msg_dropped=0",
    "PPE_HOOK set margin 0 ok heartbeat_s=360 margin_s=0 offline_timeout_s=720 t_ms=2155209",
]


class FakeSerial:
    """Answers the hub hooks the driver uses, with the real line formats."""
    def __init__(self, port=None, baud=None, timeout=0.1):
        import threading
        self.q, self.lk, self.hb, self.mg, self.timeout = b"", threading.Lock(), 360, 60, timeout
    def read(self, n=256):
        import time
        time.sleep(0.02)
        with self.lk:
            out, self.q = self.q[:n], self.q[n:]
        return out
    def push(self, lines):
        with self.lk:
            self.q += ("\n".join(lines) + "\n").encode()
    def write(self, data):
        cmd = data.decode().strip()
        if cmd == "dump":
            self.push(dump(5000, ["WORN"] * 5, hb=self.hb, margin=self.mg))
        elif cmd.startswith("set "):
            _, what, val = cmd.split()
            if what == "heartbeat": self.hb = int(val)
            else: self.mg = int(val)
            self.push(["PPE_HOOK set %s %s ok heartbeat_s=%d margin_s=%d offline_timeout_s=%d t_ms=1" % (what, val, self.hb, self.mg, 2 * self.hb + self.mg)])
    def close(self):
        pass


class Plumbing(unittest.TestCase):
    def test_real_hub_lines_parse(self):
        ev = b.events("\n".join(REAL))
        kinds = [e["kind"] for e in ev]
        self.assertEqual(kinds, ["acc", "comp", "comp", "boot"])
        self.assertEqual(ev[0]["prev"], "OFFLINE")
        self.assertTrue(b.RE_DHUB.match(REAL[4]))
        self.assertTrue(b.RE_SETOK.search(REAL[5]))

    def test_configure_and_get_dump_through_a_capture(self):
        import logger
        with tempfile.TemporaryDirectory() as d:
            cap = logger.Capture("X", 115200, os.path.join(d, "t.log"), ["h"], serial_factory=FakeSerial)
            try:
                dd = b.configure(cap, 60, 0)
                self.assertIsNotNone(dd)
                self.assertEqual(dd["hub"]["timeout_s"], 120)
                self.assertEqual(dd["nodes"][2]["state"], "WORN")
                dd = b.configure(cap, 360, 60)
                self.assertEqual(dd["hub"]["timeout_s"], 780)
            finally:
                cap.close()
    def test_parse_set_ok_line(self):
        self.assertTrue(b.RE_SETOK.search("PPE_HOOK set margin 0 ok heartbeat_s=360 margin_s=0 offline_timeout_s=720 t_ms=5"))

    def test_params(self):
        self.assertEqual(b.params(log(["PARAM case=offline node=2", "PARAM ctr=9"]))["ctr"], "9")

    def test_analyze_text_without_param_is_blocked(self):
        self.assertEqual(b.analyze_text("nothing")[0], "BLOCKED")

    def test_open_run_refuses_an_existing_log(self):
        import argparse, logger
        with tempfile.TemporaryDirectory() as root:
            old = logger.REPO
            a = argparse.Namespace(tc="TC-99", rehearsal=True, hub="COMX")
            path = logger.log_path("TC-99", sprint="scratch")
            os.makedirs(os.path.dirname(path), exist_ok=True)
            put(path, "w", "")
            try:
                with self.assertRaises(SystemExit):
                    b.open_run(a, [])
            finally:
                os.remove(path)
                try:
                    os.rmdir(os.path.dirname(path))
                except OSError:
                    pass


if __name__ == "__main__":
    unittest.main()
