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
    # ---- node lines in the log (A-20): the node's own marks judge the spacing, the observer judges the counts
    def _with_node(self, p, starts_ms, tx_line=None, seqs=1):
        lines = open(p).read().split("\n")
        extra = []
        for sq in range(seqs):
            if tx_line: extra.append("2026-11-11T10:00:05.000 | NODE [1] I node: " + tx_line)
            for k, ms in enumerate(starts_ms):
                extra.append("2026-11-11T10:00:05.100 | NODE MARK COPY%d %d" % (k, 1000000 + sq * 30000000 + ms * 1000))
        open(p, "w").write("\n".join(lines[:2] + extra + lines[2:]))
        return p
    def test_node_marks_inside_tolerance_pass_even_if_the_observer_error_would_not(self):
        p = make_log(tweak=lambda k, s: s + (60000 if k == 3 else 0))      # the observer sees a 560 ms spacing (its error is +/- 40 ms)
        self._with_node(p, [0, 500, 1000, 1500, 2000])
        rc, out = self.run_(p, need_marks=True); self.assertEqual(rc, 0, out); self.assertIn("judged", out)
    def test_node_marks_outside_tolerance_fail(self):
        p = make_log(); self._with_node(p, [0, 500, 1000, 1580, 2080])
        rc, out = self.run_(p, need_marks=True); self.assertEqual(rc, 1); self.assertIn("node seq 1: copy 4 starts 580.0 ms", out)
    def test_missing_node_marks_fail_when_they_are_needed(self):
        rc, out = self.run_(make_log(), need_marks=True); self.assertEqual(rc, 1); self.assertIn("no node copy marks", out)
    def test_node_with_four_marks_fails(self):
        p = make_log(); self._with_node(p, [0, 500, 1000, 1500])
        rc, out = self.run_(p, need_marks=True); self.assertEqual(rc, 1); self.assertIn("4 copy marks, expected 5", out)
    def test_node_and_observer_must_agree_on_the_number_of_sequences(self):
        p = make_log(); self._with_node(p, [0, 500, 1000, 1500, 2000], seqs=2)
        rc, out = self.run_(p, need_marks=True); self.assertEqual(rc, 1); self.assertIn("node sent 2 sequences, the observer saw 1 transmissions", out)
    def test_node_tx_line_must_match_the_packet_on_the_air(self):
        p = make_log(); self._with_node(p, [0, 500, 1000, 1500, 2000], tx_line="TX seq=1 type=event ctr=258 state=2 boot=0 fault=0")
        rc, out = self.run_(p, need_marks=True); self.assertEqual(rc, 0, out)
        p = make_log(); self._with_node(p, [0, 500, 1000, 1500, 2000], tx_line="TX seq=1 type=event ctr=259 state=2 boot=0 fault=0")
        rc, out = self.run_(p, need_marks=True); self.assertEqual(rc, 1); self.assertIn("the node log says event ctr=259", out)
        p = make_log(); self._with_node(p, [0, 500, 1000, 1500, 2000], tx_line="TX seq=1 type=event ctr=258 state=1 boot=1 fault=0")
        rc, out = self.run_(p, need_marks=True); self.assertEqual(rc, 1)
    # ---- TC-07 / TC-10 run: boot event, wear/remove events, heartbeats
    def _run_log(self, packets, **kw):
        hexes = [adv(flags=f, state=s, ctr=c) for (f, s, c) in packets]
        return make_log(hexes=hexes, **kw)
    GOOD = [(0x30, 1, 10), (0x20, 0, 11), (0x20, 1, 12), (0x20, 0, 13), (0x20, 1, 14), (0x00, 1, 15), (0x00, 1, 16)]
    def test_run_boot_events_and_heartbeats_pass(self):
        rc, out = self.run_(self._run_log(self.GOOD), cycles=2, heartbeats=2); self.assertEqual(rc, 0, out); self.assertIn("2 wear + 2 remove events and 2 heartbeats", out)
    def test_run_counter_gap_fails(self):
        p = list(self.GOOD); p[3] = (0x20, 0, 20)
        rc, out = self.run_(self._run_log(p), cycles=2, heartbeats=2); self.assertEqual(rc, 1); self.assertIn("counter 20 after 12, it must be +1", out)
    def test_run_state_that_does_not_alternate_fails(self):
        p = list(self.GOOD); p[2] = (0x20, 0, 12)
        rc, out = self.run_(self._run_log(p), cycles=2, heartbeats=2); self.assertEqual(rc, 1); self.assertIn("it must alternate", out)
    def test_run_missing_boot_flag_fails(self):
        p = list(self.GOOD); p[0] = (0x20, 1, 10)
        rc, out = self.run_(self._run_log(p), cycles=2, heartbeats=2); self.assertEqual(rc, 1); self.assertIn("TX 1 must be the BOOT=1 event", out)
    def test_run_boot_flag_on_a_later_event_fails(self):
        p = list(self.GOOD); p[2] = (0x30, 1, 12)
        rc, out = self.run_(self._run_log(p), cycles=2, heartbeats=2); self.assertEqual(rc, 1); self.assertIn("event with BOOT=0", out)
    def test_run_missing_or_extra_transmission_fails(self):
        rc, out = self.run_(self._run_log(self.GOOD[:-1]), cycles=2, heartbeats=2); self.assertEqual(rc, 1); self.assertIn("expected 7 transmissions", out)
        rc, out = self.run_(self._run_log(self.GOOD + [(0x00, 1, 17)]), cycles=2, heartbeats=2); self.assertEqual(rc, 1)
    def test_run_heartbeat_with_the_wrong_state_or_type_fails(self):
        p = list(self.GOOD); p[5] = (0x00, 0, 15)
        rc, out = self.run_(self._run_log(p), cycles=2, heartbeats=2); self.assertEqual(rc, 1); self.assertIn("heartbeat state 0", out)
        p = list(self.GOOD); p[6] = (0x20, 1, 16)
        rc, out = self.run_(self._run_log(p), cycles=2, heartbeats=2); self.assertEqual(rc, 1); self.assertIn("must be a heartbeat", out)
    def test_plan_generator_writes_timed_hook_lines_and_the_loader_reads_them_back(self):
        import io, contextlib, argparse
        d = tempfile.mkdtemp(); f = os.path.join(d, "plan.txt")
        ns = argparse.Namespace(cycles=2, hold=6.0, first=32.0, grace=2, hb=60, heartbeats=20, out=f)
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf): self.assertEqual(observe.cmd_plan(ns), 0)
        plan = observe.load_plan(f)
        self.assertEqual(plan[0], (30.0, "set grace 2"))
        self.assertEqual([c for _t, c in plan[1:5]], ["adc 50", "adc 2000", "adc 50", "adc 2000"])
        self.assertEqual(plan[1][0], 32.0); self.assertEqual(plan[2][0], 38.0); self.assertEqual(plan[4][0], 50.0)
        self.assertEqual([c for _t, c in plan[-2:]], ["set hb 60", "tx hb"])
        self.assertIn("run at least --seconds", buf.getvalue())
    def test_plan_loader_refuses_anything_that_looks_like_a_key(self):
        d = tempfile.mkdtemp(); f = os.path.join(d, "p.txt")
        open(f, "w").write("5 ident keyload\n")
        with self.assertRaises(SystemExit): observe.load_plan(f)
        open(f, "w").write("5 " + "a" * 64 + "\n")
        with self.assertRaises(SystemExit): observe.load_plan(f)
        open(f, "w").write("5 ident testkey\n6 adc 50\n")
        self.assertEqual(observe.load_plan(f), [(5.0, "ident testkey"), (6.0, "adc 50")])
    # ---- TC-01: node_id from the air, and silence from an unprovisioned board
    def test_node_id_on_the_air_must_match_the_provisioned_one(self):
        rc, out = self.run_(make_log(), node_id=3); self.assertEqual(rc, 0, out)
        rc, out = self.run_(make_log(), node_id=2); self.assertEqual(rc, 1); self.assertIn("node_id 3 on the air, provisioned as 2", out)
    def test_an_unprovisioned_board_must_send_nothing(self):
        rc, out = self.run_(make_log(copies=0), expect_tx=0); self.assertEqual(rc, 0, out)
        rc, out = self.run_(make_log(), expect_tx=0); self.assertEqual(rc, 1); self.assertIn("expected 0 transmissions, found 1", out)
    def test_an_empty_log_without_expect_tx_zero_still_fails(self):
        rc, out = self.run_(make_log(copies=0)); self.assertEqual(rc, 1); self.assertIn("no matching transmission", out)
    # ---- a sleeping node: the port goes away, commands can be lost
    def test_a_command_that_could_not_be_sent_fails_the_run(self):
        p = make_log(); lines = open(p).read().split("\n")
        lines.insert(2, "2026-11-11T10:00:05.000 | NODETX FAILED (the node was asleep or its port closed): adc 50")
        open(p, "w").write("\n".join(lines))
        rc, out = self.run_(p); self.assertEqual(rc, 1); self.assertIn("a node command was lost: (the node was asleep or its port closed): adc 50", out)
    def test_node_port_notes_do_not_disturb_the_analysis(self):
        p = make_log(); lines = open(p).read().split("\n")
        lines.insert(2, "2026-11-11T10:00:05.000 | NODEPORT closed (node asleep or restarting)")
        lines.insert(3, "2026-11-11T10:00:06.000 | NODEPORT open")
        open(p, "w").write("\n".join(lines))
        rc, out = self.run_(p); self.assertEqual(rc, 0, out)
    # ---- capture robustness: a quiet observer stops the run, a reported failure fails the analysis
    def test_last_sec_reads_the_newest_number(self):
        d = tempfile.mkdtemp(); p = os.path.join(d, "l.log")
        open(p, "w").write("x | SEC 5 seen=1\nx | PKT 1.0 21 3 -50 aa\nx | SEC 6 seen=2\n")
        self.assertEqual(observe.last_sec(p), 6)
        open(p, "w").write("nothing here\n"); self.assertIsNone(observe.last_sec(p))
        self.assertIsNone(observe.last_sec(os.path.join(d, "missing.log")))
    def test_watchdog_stops_a_capture_when_the_observer_goes_quiet(self):
        import threading
        d = tempfile.mkdtemp(); p = os.path.join(d, "l.log"); open(p, "w").write("x | SEC 5 seen=1\n")
        notes = []
        class Cap:
            def note(self, t): notes.append(t)
        stop, stalled = threading.Event(), threading.Event()
        th = threading.Thread(target=observe.observer_watchdog, args=(p, Cap(), stop, stalled, 0.3, 0.05)); th.start()
        th.join(3.0); stop.set()
        self.assertTrue(stalled.is_set()); self.assertIn("OBSERVER STALLED", notes[0])
    def test_wait_until_returns_early_when_stalled_and_at_the_time_otherwise(self):
        import threading, time
        st = threading.Event(); st.set()
        t = time.time(); self.assertFalse(observe.wait_until(time.time() + 5, st)); self.assertTrue(time.time() - t < 1.0)
        st2 = threading.Event(); self.assertTrue(observe.wait_until(time.time() + 0.2, st2))
    def test_a_stalled_observer_or_failed_reset_fails_the_analysis(self):
        for note in ("OBSERVER STALLED: no SEC line for 8 s", "NODE RESET FAILED (no node port)", "final TIME and STAT could not be sent"):
            p = make_log(); lines = open(p).read().split("\n"); lines.insert(2, "2026-11-11T10:00:05.000 | " + note)
            open(p, "w").write("\n".join(lines))
            rc, out = self.run_(p); self.assertEqual(rc, 1, note); self.assertIn("the capture reports", out)
    def test_send_node_retries_until_the_port_is_back_and_then_notes_it(self):
        import threading
        notes = []
        class Cap:
            def note(self, t): notes.append(t)
        class Ser:
            def __init__(self): self.n = 0; self.sent = []
            def write(self, b):
                self.n += 1
                if self.n < 3: raise OSError("port closed")
                self.sent.append(b)
        h = {"ser": Ser()}
        self.assertTrue(observe.send_node(h, "adc 50", Cap(), threading.Event(), limit=5.0))
        self.assertEqual(h["ser"].sent, [b"adc 50\n"]); self.assertIn("sent after 3 tries", notes[0])
    def test_send_node_gives_up_and_logs_a_failure(self):
        import threading
        notes = []
        class Cap:
            def note(self, t): notes.append(t)
        h = {"ser": None}
        self.assertFalse(observe.send_node(h, "set hb 60", Cap(), threading.Event(), limit=0.5))
        self.assertIn("NODETX FAILED", notes[-1])
    # ---- process rule 2: the node must run the firmware of HEAD
    def _fw_log(self, found="common=aaaaaaaa node=bbbbbbbb dirty=0", expected="common=aaaaaaaa node=bbbbbbbb", required=True):
        p = make_log(); lines = open(p).read().split("\n")
        extra = []
        if required: extra += ["# fw_check=required", "# fw_expected " + expected]
        if found: extra.append("2026-11-11T10:00:05.000 | NODE FW " + found + " type=0 hooks=1")
        open(p, "w").write("\n".join(lines[:1] + extra + lines[1:]))
        return p
    def test_fw_parse_and_problem(self):
        self.assertEqual(observe.parse_fw("x | NODE FW common=0000000a node=0000000b dirty=0 type=0 hooks=1"), (10, 11, 0))
        self.assertIsNone(observe.parse_fw("nothing"))
        self.assertIsNone(observe.fw_problem((10, 11, 0), (10, 11, 0)))
        self.assertIn("no firmware identity", observe.fw_problem(None, (1, 2, 0)))
        self.assertIn("dirty=1", observe.fw_problem((1, 2, 1), (1, 2, 0)))
        self.assertIn("HEAD is", observe.fw_problem((1, 3, 0), (1, 2, 0)))
    def test_analysis_passes_when_the_node_reports_the_firmware_of_head(self):
        rc, out = self.run_(self._fw_log()); self.assertEqual(rc, 0, out)
    def test_analysis_fails_without_a_firmware_line_when_one_is_required(self):
        rc, out = self.run_(self._fw_log(found=None)); self.assertEqual(rc, 1); self.assertIn("firmware identity: the node sent no firmware identity line", out)
    def test_analysis_fails_on_another_firmware_or_a_dirty_build(self):
        rc, out = self.run_(self._fw_log(found="common=aaaaaaaa node=cccccccc dirty=0")); self.assertEqual(rc, 1); self.assertIn("HEAD is", out)
        rc, out = self.run_(self._fw_log(found="common=aaaaaaaa node=bbbbbbbb dirty=1")); self.assertEqual(rc, 1); self.assertIn("dirty=1", out)
    def test_logs_without_the_requirement_are_not_checked(self):
        rc, out = self.run_(self._fw_log(found=None, required=False)); self.assertEqual(rc, 0, out)
    def test_a_reported_mismatch_note_fails_the_analysis(self):
        p = make_log(); lines = open(p).read().split("\n"); lines.insert(2, "2026-11-11T10:00:05.000 | FIRMWARE MISMATCH: x")
        open(p, "w").write("\n".join(lines)); rc, out = self.run_(p); self.assertEqual(rc, 1); self.assertIn("FIRMWARE MISMATCH", out)
    # ---- node reset: the pulse must reach the chip, and the node must prove it restarted
    def test_reset_pulse_writes_dtr_after_every_rts_change(self):
        ops = []
        class Ser:
            def __setattr__(self, k, v): ops.append((k, v))
        observe.reset_pulse(Ser(), hold=0.0)
        self.assertEqual(ops, [("dtr", False), ("rts", True), ("dtr", False), ("rts", False), ("dtr", False)])
    def test_plan_reset_retries_while_the_port_is_closed_and_logs_a_failure_at_the_end(self):
        import threading
        notes = []
        class Cap:
            def note(self, t): notes.append(t)
        class Ser:
            def __init__(self): self.ops = []
            def __setattr__(self, k, v):
                if k == "ops": object.__setattr__(self, k, v)
                else: self.ops.append((k, v))
        h = {"ser": None}
        def later(): import time; time.sleep(0.3); h["ser"] = Ser()
        threading.Thread(target=later).start()
        self.assertTrue(observe.send_reset(h, Cap(), threading.Event(), limit=5.0))
        self.assertEqual(notes, [observe.PLAN_RESET_NOTE]); self.assertIn(("rts", True), h["ser"].ops)
        h2 = {"ser": None}; notes.clear()
        self.assertFalse(observe.send_reset(h2, Cap(), threading.Event(), limit=0.3))
        self.assertIn("NODE RESET FAILED", notes[-1])
    SHOW = ("x | NODE NODE run=active prov=1 node=1 vest=1 key=yes fault=none radio=0 ctr=5 ceil=64 tx_state=0 skip=0 boot_us=%d first_tx_us=0\n"
            "x | NODE SLEEP on=1 wake=%s resumed=0 noop=0 poll=0 wakes=0 awake_until_ms=0 now_ms=%d\n")
    def test_reset_check_accepts_a_fresh_cold_boot(self):
        self.assertIsNone(observe.reset_problem(self.SHOW % (7000, "boot", 2900), 4000))
    def test_reset_check_rejects_a_node_that_was_already_up(self):
        self.assertIn("did not reach", observe.reset_problem(self.SHOW % (7000, "boot", 9100), 4000))
    def test_reset_check_rejects_a_wake_from_sleep_and_a_missing_reply(self):
        self.assertIn("wake=gpio", observe.reset_problem(self.SHOW % (7000, "gpio", 900), 4000))
        self.assertIn("did not answer", observe.reset_problem("x | NODE MARK RAW 1 state=2 fault=0\n", 4000))
    def test_reset_check_handles_the_32_bit_boot_us_wrap(self):
        # TC-11b at 0e9051e: persistent clock 4564932 ms, boot_us printed as (g_boot_ms * 1000) mod 2^32 = 267447704
        self.assertIsNone(observe.reset_problem(self.SHOW % (267447704, "boot", 4564932), 4012))
        self.assertIn("did not reach", observe.reset_problem(self.SHOW % (267447704, "boot", 4564932 + 9000), 4012))
    def test_reset_check_uses_the_last_show_reply(self):
        self.assertIsNone(observe.reset_problem(self.SHOW % (7000, "boot", 90000) + self.SHOW % (5, "boot", 3000), 4000))
    # ---- prompts for the person at the bench (glove and helmet runs)
    def test_plan_say_lines_are_prompts_and_may_be_long(self):
        d = tempfile.mkdtemp(); f = os.path.join(d, "p.txt")
        txt = "PRESS and HOLD the button NOW (cycle 1: wear), keep it pressed until the next message appears"
        open(f, "w").write("# c\n40 say " + txt + "\n50 adc 50\n")
        self.assertEqual(observe.load_plan(f), [(40.0, "say " + txt), (50.0, "adc 50")])
    def test_a_long_command_that_is_not_a_prompt_is_still_refused(self):
        d = tempfile.mkdtemp(); f = os.path.join(d, "p.txt")
        open(f, "w").write("40 adc " + "5" * 60 + "\n")
        with self.assertRaises(SystemExit): observe.load_plan(f)
    # ---- the reset is only sent to a node that has stayed awake
    def test_wait_port_stable_needs_the_port_open_for_the_whole_time(self):
        import threading, time
        h = {"ser": None}
        def flip():
            time.sleep(0.2); h["ser"] = object()
            time.sleep(0.15); h["ser"] = None          # the node fell back to sleep before it was stable
            time.sleep(0.3); h["ser"] = object()      # and woke again
        threading.Thread(target=flip, daemon=True).start()
        t = time.time(); self.assertTrue(observe.wait_port_stable(h, 5.0, stable=0.5)); self.assertTrue(time.time() - t >= 0.9)
    def test_wait_port_stable_gives_up_when_the_port_never_stays(self):
        self.assertFalse(observe.wait_port_stable({"ser": None}, 0.3, stable=0.1))
if __name__ == "__main__": unittest.main()
