"""Unit tests of tools/s5_motor.py (no hardware, no pyserial needed)."""
import os, sys, unittest
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import s5_motor as m


def train(on_us, off_us, count, t0=1000000, ch=1, jitter=None, drop=None):
    """E lines of a pulse train; jitter = {edge index: extra us}; drop = set of edge indexes the recorder missed."""
    lines, t, k = [], t0, 0
    for i in range(count):
        for lvl, dur in ((1, on_us), (0, off_us)):
            if k not in (drop or set()):
                lines.append("E %d %d %d" % ((t + (jitter or {}).get(k, 0)) & 0xFFFFFFFF, ch, lvl))
            k += 1
            t += dur
    return lines


def log(lines, pc=True):
    return "\n".join(("2026-11-15T10:00:00.000 | %s" % l if pc else l) for l in lines)


class Cal(unittest.TestCase):
    def test_exact_train_passes(self):
        v, msg = m.analyze_cal(log(train(300000, 700000, 20)), 300, 700, 20)
        self.assertEqual(v, "PASS", msg)

    def test_small_errors_pass_and_are_reported(self):
        v, msg = m.analyze_cal(log(train(300000, 700000, 20, jitter={3: 400, 10: -250})), 300, 700, 20)
        self.assertEqual(v, "PASS"); self.assertIn("worst case", msg)

    def test_error_above_1_ms_fails(self):
        v, _ = m.analyze_cal(log(train(300000, 700000, 20, jitter={5: 1500})), 300, 700, 20)
        self.assertEqual(v, "FAIL")

    def test_a_missed_edge_fails(self):
        self.assertEqual(m.analyze_cal(log(train(300000, 700000, 20, drop={7})), 300, 700, 20)[0], "FAIL")

    def test_repeated_level_with_the_right_edge_count_fails(self):
        lines = train(300000, 700000, 20)
        lines[9] = lines[9][:-1] + ("0" if lines[9].endswith("1") else "1")      # one edge reports the wrong level
        v, msg = m.analyze_cal(log(lines), 300, 700, 20)
        self.assertEqual(v, "FAIL"); self.assertIn("repeated", msg)

    def test_wrong_pulse_length_fails(self):
        self.assertEqual(m.analyze_cal(log(train(310000, 700000, 20)), 300, 700, 20)[0], "FAIL")

    def test_wrong_count_fails(self):
        self.assertEqual(m.analyze_cal(log(train(300000, 700000, 19)), 300, 700, 20)[0], "FAIL")

    def test_no_edges_is_blocked(self):
        self.assertEqual(m.analyze_cal(log(["PONG", "READY"]), 300, 700, 20)[0], "BLOCKED")

    def test_clock_wrap_is_handled(self):
        self.assertEqual(m.analyze_cal(log(train(300000, 700000, 20, t0=0xFFFFFFFF - 3000000)), 300, 700, 20)[0], "PASS")

    def test_other_channel_is_ignored(self):
        lines = train(300000, 700000, 20) + train(123000, 456000, 3, ch=0)
        self.assertEqual(m.analyze_cal(log(lines), 300, 700, 20)[0], "PASS")

    def test_analyze_text_uses_the_param_line(self):
        text = log(["PARAM case=cal on_ms=300 off_ms=700 count=20"] + train(300000, 700000, 20))
        self.assertEqual(m.analyze_text(text)[0], "PASS")
        self.assertEqual(m.analyze_text(log(train(300000, 700000, 20)))[0], "BLOCKED")

    def test_raw_lines_without_pc_prefix(self):
        self.assertEqual(m.analyze_cal(log(train(300000, 700000, 20), pc=False), 300, 700, 20)[0], "PASS")


# ---------------- TC-29 scenarios: hub ACC / NODE_OFFLINE lines, marker pulses and motor edges on the recorder clock
def acc_line(t_ms, node, ctr, state, prev="WORN"):
    return "ACC t_ms=%d t_us=%d node=%d ctr=%d state=%d batt=90 flags=0x60 missed=0 prev=%s now=%s" % (
        t_ms, t_ms * 1000, node, ctr, state, prev, "WORN" if state else "NOT_WORN")


def offline_line(t_ms, node, age_s):
    return "MSG msg_type=NODE_OFFLINE vest_id=1 node_id=%d state=OFFLINE last_seen_age_s=%d t_ms=%d" % (node, age_s, t_ms)


OFFSET = 7000000      # recorder clock = hub ms * 1000 + OFFSET + 1 ms (the marker rises 1 ms after the accept stamp)


def marker(t_ms):
    t = t_ms * 1000 + OFFSET + 1000
    return ["E %d 1 1" % t, "E %d 1 0" % (t + 1000)]


def pattern(t_start_us, cycles, on=300000, off=700000, last_on_cut=None):
    out, t = [], t_start_us
    for i in range(cycles):
        out.append("E %d 0 1" % t)
        t += on
        out.append("E %d 0 0" % t)
        t += off
    return out, t


def tc29a_log(start_ms=2, cycles=61, on=300000, off=700000, stop_after_ms=None, restart=False, drop_marker=False, stop_in_off=False, restart_ms=5000, never_stops=False):
    """cycles full 1 s cycles run before the WORN packet; the packet arrives 100 ms into the next ON (or 500 ms in, in the OFF part)."""
    hub = ["PPE_HOOK marker on ok pin=6 t_ms=100", acc_line(500, 2, 90, 1), acc_line(1000, 2, 100, 0)]
    rec = marker(500) + marker(1000)
    t_rise = 1000 * 1000 + OFFSET + 1000 + start_ms * 1000
    me, _ = pattern(t_rise, cycles + 1, on, off)
    stop_ms = 1000 + cycles * 1000 + start_ms + (500 if stop_in_off else 100)
    hub.append(acc_line(stop_ms, 2, 101, 1, "NOT_WORN"))
    stop_marker = marker(stop_ms)
    t_stop = stop_ms * 1000 + OFFSET + 1000
    cut = [e for e in me if int(e.split()[1]) < t_stop]
    if cut[-1].endswith(" 0 1") and not never_stops:               # running ON: the motor goes low after the packet
        cut.append("E %d 0 0" % (t_stop + (stop_after_ms or 2) * 1000))
    if restart:
        cut.append("E %d 0 1" % (t_stop + restart_ms * 1000))
    if drop_marker:
        stop_marker = []
    return "\n".join(hub + rec + cut + stop_marker)


class Tc29a(unittest.TestCase):
    def test_pass(self):
        v, msg = m.analyze_tc29a(tc29a_log(), 2, 100, 101)
        self.assertEqual(v, "PASS", msg)

    def test_start_latency_over_200_ms_fails(self):
        self.assertEqual(m.analyze_tc29a(tc29a_log(start_ms=250), 2, 100, 101)[0], "FAIL")
        self.assertEqual(m.analyze_tc29a(tc29a_log(start_ms=199), 2, 100, 101)[0], "PASS")

    def test_on_and_off_tolerances(self):
        self.assertEqual(m.analyze_tc29a(tc29a_log(on=335000), 2, 100, 101)[0], "FAIL")
        self.assertEqual(m.analyze_tc29a(tc29a_log(on=329000, off=700000), 2, 100, 101)[0], "PASS")
        self.assertEqual(m.analyze_tc29a(tc29a_log(off=740000), 2, 100, 101)[0], "FAIL")
        self.assertEqual(m.analyze_tc29a(tc29a_log(off=669000), 2, 100, 101)[0], "FAIL")
        self.assertEqual(m.analyze_tc29a(tc29a_log(off=671000), 2, 100, 101)[0], "PASS")

    def test_too_few_cycles_fail(self):
        self.assertEqual(m.analyze_tc29a(tc29a_log(cycles=40), 2, 100, 101)[0], "FAIL")

    def test_motor_restarting_after_the_worn_packet_fails(self):
        self.assertEqual(m.analyze_tc29a(tc29a_log(restart=True), 2, 100, 101)[0], "FAIL")

    def test_restart_within_the_stop_time_fails(self):
        self.assertEqual(m.analyze_tc29a(tc29a_log(restart=True, restart_ms=600), 2, 100, 101)[0], "FAIL")
        self.assertEqual(m.analyze_tc29a(tc29a_log(stop_in_off=True, restart=True, restart_ms=600), 2, 100, 101)[0], "FAIL")

    def test_motor_that_never_stops_fails(self):
        v, msg = m.analyze_tc29a(tc29a_log(never_stops=True), 2, 100, 101)
        self.assertEqual(v, "FAIL"); self.assertIn("stayed on", msg)

    def test_stop_later_than_1_s_fails(self):
        self.assertEqual(m.analyze_tc29a(tc29a_log(stop_after_ms=1200), 2, 100, 101)[0], "FAIL")
        self.assertEqual(m.analyze_tc29a(tc29a_log(stop_after_ms=950), 2, 100, 101)[0], "PASS")

    def test_stop_in_the_off_part_counts_as_already_low(self):
        v, msg = m.analyze_tc29a(tc29a_log(stop_in_off=True), 2, 100, 101)
        self.assertEqual(v, "PASS", msg); self.assertIn("already low", msg)
        self.assertEqual(m.analyze_tc29a(tc29a_log(stop_in_off=True, restart=True), 2, 100, 101)[0], "FAIL")

    def test_mismatching_marker_count_is_blocked(self):
        self.assertEqual(m.analyze_tc29a(tc29a_log(drop_marker=True), 2, 100, 101)[0], "BLOCKED")

    def test_motor_never_started_fails(self):
        v, _ = m.analyze_tc29a("\n".join(l for l in tc29a_log().splitlines() if not (l.startswith("E") and l.split()[2] == "0")), 2, 100, 101)
        self.assertEqual(v, "FAIL")


class Stale(unittest.TestCase):
    def noise(self, ch, t0=2000000, n=300):
        return ["E %d %d %d" % (t0 + i * 3, ch, (i + 1) % 2) for i in range(n)]

    def test_stale_noise_before_the_recorder_reset_is_ignored(self):
        text = "\n".join(self.noise(1) + self.noise(0) + ["READY"] + tc29a_log().splitlines())
        self.assertEqual(m.analyze_tc29a(text, 2, 100, 101)[0], "PASS")
        text = "\n".join(self.noise(1) + ["READY"] + tc29c_log().splitlines())
        self.assertEqual(m.analyze_tc29c(text, 2, 77, 780)[0], "PASS")

    def test_noise_after_the_reset_still_blocks(self):
        text = "\n".join(["READY"] + tc29a_log().splitlines() + self.noise(1, t0=9000000))
        self.assertEqual(m.analyze_tc29a(text, 2, 100, 101)[0], "BLOCKED")

    def test_only_the_last_reset_counts(self):
        text = "\n".join(["READY"] + self.noise(1) + ["READY"] + tc29c_log().splitlines())
        self.assertEqual(m.analyze_tc29c(text, 2, 77, 780)[0], "PASS")

    def test_the_marker_pairing_ignores_stale_pulses_too(self):
        stale = marker(10) + marker(20) + marker(30)
        text = "\n".join(stale + ["READY"] + tc29c_log().splitlines())
        self.assertEqual(m.analyze_tc29c(text, 2, 77, 780)[0], "PASS")


class Noise(unittest.TestCase):
    def noisy_marker(self):
        return ["E %d 1 %d" % (3000000 + i * 4, (i + 1) % 2) for i in range(10)]

    def test_noisy_marker_channel_blocks_every_analyzer_with_a_clear_message(self):
        v, msg = m.analyze_tc29a(tc29a_log() + "\n" + "\n".join(self.noisy_marker()), 2, 100, 101)
        self.assertEqual(v, "BLOCKED"); self.assertIn("marker channel", msg); self.assertIn("floating", msg)
        v, msg = m.analyze_tc29c(tc29c_log() + "\n" + "\n".join(self.noisy_marker()), 2, 77, 780)
        self.assertEqual(v, "BLOCKED"); self.assertIn("marker channel", msg)
        v, msg = m.analyze_cal(log(train(300000, 700000, 20) + self.noisy_marker()), 300, 700, 20)
        self.assertEqual(v, "BLOCKED")

    def test_noisy_motor_channel_is_blocked_not_a_hub_failure(self):
        noise = ["E %d 0 %d" % (9000000 + i * 3, (i + 1) % 2) for i in range(6)]
        v, msg = m.analyze_tc29a(tc29a_log() + "\n" + "\n".join(noise), 2, 100, 101)
        self.assertEqual(v, "BLOCKED"); self.assertIn("motor channel", msg)

    def test_pairing_mismatch_message_has_the_counts(self):
        v, msg = m.analyze_tc29a(tc29a_log(drop_marker=True), 2, 100, 101)
        self.assertEqual(v, "BLOCKED"); self.assertIn("accepted packets but", msg)

    def test_an_alert_that_ended_before_the_first_packet_is_fine_but_a_running_one_is_not(self):
        base = tc29a_log().splitlines()
        # an earlier alert: motor edges 5 s before the NOT_WORN marker, ending low: allowed
        t1 = 1000 * 1000 + OFFSET + 1000
        early = ["E %d 0 1" % (t1 - 6000000), "E %d 0 0" % (t1 - 5700000)]
        self.assertEqual(m.analyze_tc29a("\n".join(base[:3] + early + base[3:]), 2, 100, 101)[0], "PASS")
        running = ["E %d 0 1" % (t1 - 400000)]
        self.assertEqual(m.analyze_tc29a("\n".join(base[:3] + running + base[3:]), 2, 100, 101)[0], "BLOCKED")


class Tc29b(unittest.TestCase):
    def log(self, edges_after=0, states="UNKNOWN", alert=0, observe="65", boots=1, ready=True):
        L = ["PARAM case=tc29b observe_s=%s" % observe]
        if ready: L.append("READY")
        L += ["BOOT reset_reason=1", "MSG msg_type=HUB_BOOT vest_id=1 reset_reason=1 fw=s5-hub-logic t_ms=341"] * boots
        L += ["DUMP begin t_ms=3400", "DUMP hub vest=0x01 accept_enabled=1 registry_ok=1 vest_state=NON_COMPLIANT alert_active=%d heartbeat_s=360 margin_s=60 offline_timeout_s=780 msg_pending=0 msg_dropped=0" % alert]
        L += ["DUMP node=%d state=%s last_counter=1 last_seen_ms=341 accepted=0 dup=0 replay=0 gap=0 hmac_fail=0 battery=0 flags=0x00" % (i, states) for i in range(1, 6)] + ["DUMP end"]
        L += ["E %d 0 %d" % (5000000 + i * 100000, (i + 1) % 2) for i in range(edges_after)]
        return "\n".join(L)

    def test_pass(self):
        self.assertEqual(m.analyze_tc29b(self.log())[0], "PASS")

    def test_any_motor_edge_fails(self):
        self.assertEqual(m.analyze_tc29b(self.log(edges_after=1))[0], "FAIL")

    def test_known_states_or_alert_fail(self):
        self.assertEqual(m.analyze_tc29b(self.log(states="WORN"))[0], "FAIL")
        self.assertEqual(m.analyze_tc29b(self.log(alert=1))[0], "FAIL")

    def test_noise_on_the_unused_marker_channel_after_a_reset_is_ignored(self):
        noise = "\n".join("E %d 1 %d" % (4000000 + i * 3, (i + 1) % 2) for i in range(800))
        self.assertEqual(m.analyze_tc29b(self.log() + "\n" + noise)[0], "PASS")

    def test_a_floating_motor_pin_is_a_fail_with_the_b01_hint_not_a_setup_error(self):
        noise = "\n".join("E %d 0 %d" % (4000000 + i * 3, (i + 1) % 2) for i in range(40))
        v, msg = m.analyze_tc29b(self.log() + "\n" + noise)
        self.assertEqual(v, "FAIL"); self.assertIn("B-01", msg); self.assertIn("40 motor edges", msg)

    def test_short_observation_or_two_boots_or_no_ready_is_blocked(self):
        self.assertEqual(m.analyze_tc29b(self.log(observe="30"))[0], "BLOCKED")
        self.assertEqual(m.analyze_tc29b(self.log(boots=2))[0], "BLOCKED")
        self.assertEqual(m.analyze_tc29b(self.log(ready=False))[0], "BLOCKED")


def tc29c_log(lat_ms=3, off_at_ms=781000, early_edge=False, no_motor=False, last_ms=1000, priming_blink=True):
    hub = ["PPE_HOOK marker on ok pin=6 t_ms=100", acc_line(last_ms, 2, 77, 1, "WORN"), offline_line(last_ms + off_at_ms, 2, off_at_ms // 1000)]
    rec = marker(last_ms)
    t_last = last_ms * 1000 + OFFSET + 1000
    t_off = (last_ms + off_at_ms) * 1000 + OFFSET + 1000
    me = []
    if priming_blink:                      # an earlier alert is still ending in the first second after the packet
        me += ["E %d 0 1" % (t_last + 100000), "E %d 0 0" % (t_last + 400000), "E %d 0 1" % (t_last + 1100000)]
    if early_edge:
        me += ["E %d 0 1" % (t_off - 900000), "E %d 0 0" % (t_off - 600000)]
    if not no_motor:
        me.append("E %d 0 1" % (t_off + lat_ms * 1000))
    return "\n".join(hub + rec + me)


class Tc29c(unittest.TestCase):
    def test_pass(self):
        v, msg = m.analyze_tc29c(tc29c_log(), 2, 77, 780)
        self.assertEqual(v, "PASS", msg)

    def test_motor_a_few_ms_before_the_transition_stamp_is_clock_mapping_noise_not_a_failure(self):
        self.assertEqual(m.analyze_tc29c(tc29c_log(lat_ms=-2), 2, 77, 780)[0], "PASS")
        self.assertEqual(m.analyze_tc29c(tc29c_log(lat_ms=-40), 2, 77, 780)[0], "PASS")

    def test_the_real_log_shape_with_priming_blinks_passes(self):
        v, msg = m.analyze_tc29c(tc29c_log(priming_blink=True, lat_ms=-2), 2, 77, 780)
        self.assertEqual(v, "PASS", msg)

    def test_latency_limits(self):
        self.assertEqual(m.analyze_tc29c(tc29c_log(lat_ms=199), 2, 77, 780)[0], "PASS")
        self.assertEqual(m.analyze_tc29c(tc29c_log(lat_ms=250), 2, 77, 780)[0], "FAIL")

    def test_motor_before_offline_fails(self):
        self.assertEqual(m.analyze_tc29c(tc29c_log(early_edge=True), 2, 77, 780)[0], "FAIL")

    def test_no_motor_fails(self):
        self.assertEqual(m.analyze_tc29c(tc29c_log(no_motor=True), 2, 77, 780)[0], "FAIL")

    def test_no_offline_line_fails(self):
        text = "\n".join(l for l in tc29c_log().splitlines() if "NODE_OFFLINE" not in l)
        self.assertEqual(m.analyze_tc29c(text, 2, 77, 780)[0], "FAIL")

    def test_unknown_last_packet_is_blocked(self):
        self.assertEqual(m.analyze_tc29c(tc29c_log(), 2, 999, 780)[0], "BLOCKED")


class Dispatch(unittest.TestCase):
    def test_analyze_text_dispatches_tc29_cases(self):
        text = "PARAM case=tc29c node=2 ctr=77 timeout_s=780\n" + tc29c_log()
        self.assertEqual(m.analyze_text(text)[0], "PASS")
        text = "PARAM case=tc29a node=2 ctr_nw=100 ctr_w=101\n" + tc29a_log()
        self.assertEqual(m.analyze_text(text)[0], "PASS")


# ---------------- TC-30 scenarios ----------------
def pls(t, n, on=300000, off=700000, ch=0):
    out = []
    for _ in range(n):
        out += ["E %d %d 1" % (t & 0xFFFFFFFF, ch), "E %d %d 0" % ((t + on) & 0xFFFFFFFF, ch)]
        t += on + off
    return out, t


def keypulses(t, periods, on=100000, off=100000, low=1800000):
    out = []
    for _ in range(periods):
        for k in range(3):
            out += ["E %d 0 1" % t, "E %d 0 0" % (t + on)]
            t += on + (off if k < 2 else low)
    return out, t


def fault_line(code, t_ms):
    return "MSG msg_type=HUB_FAULT vest_id=1 code=%s t_ms=%d" % (code, t_ms)


def boot_line(t_ms=343, reason=3):
    return "MSG msg_type=HUB_BOOT vest_id=1 reset_reason=%d fw=s5-hub-logic t_ms=%d" % (reason, t_ms)


def tc30a_log(cut=395000, nw_delay=900, gd_delay=1300, nw_msg=True, gd_msg=True, n_before=4, n_after=4, extra_long=False, cut_started_before_hook=False,
              cut_ended_before_hook=False):
    t = 5000000
    L, t = pls(t, n_before)
    L.append("PPE_HOOK fault nvs-write: the next NVS write fails t_ms=1000")
    if nw_msg: L.append(fault_line("NVS_WRITE", 1000 + nw_delay))
    p, t = pls(t, 3); L += p
    guard_hook = "PPE_HOOK fault guard: while the motor is ON the pattern task stalls for 1.5 s (needs an active alert) t_ms=5000"
    if cut_ended_before_hook:
        L += ["E %d 0 1" % t, "E %d 0 0" % (t + cut), guard_hook]
    elif cut_started_before_hook:
        L += ["E %d 0 1" % t, guard_hook, "E %d 0 0" % (t + cut)]      # the real order: the motor was already ON when the fault arrived
    else:
        L += [guard_hook, "E %d 0 1" % t, "E %d 0 0" % (t + cut)]
    t += cut + 1100000
    if gd_msg: L.append(fault_line("GUARD", 5000 + gd_delay))
    p, t = pls(t, n_after); L += p
    if extra_long:
        L += ["E %d 0 1" % t, "E %d 0 0" % (t + 420000)]
    return "\n".join(L)


class Tc30a(unittest.TestCase):
    def test_pass(self):
        v, msg = m.analyze_tc30a(tc30a_log())
        self.assertEqual(v, "PASS", msg)

    def test_missing_or_late_fault_messages_fail(self):
        self.assertEqual(m.analyze_tc30a(tc30a_log(nw_msg=False))[0], "FAIL")
        self.assertEqual(m.analyze_tc30a(tc30a_log(nw_delay=3500))[0], "FAIL")
        self.assertEqual(m.analyze_tc30a(tc30a_log(gd_msg=False))[0], "FAIL")
        self.assertEqual(m.analyze_tc30a(tc30a_log(gd_delay=4500))[0], "FAIL")

    def test_guard_cut_limits(self):
        self.assertEqual(m.analyze_tc30a(tc30a_log(cut=401000))[0], "FAIL")           # above 400 ms
        self.assertEqual(m.analyze_tc30a(tc30a_log(cut=400000))[0], "PASS")
        self.assertEqual(m.analyze_tc30a(tc30a_log(cut=379000))[0], "FAIL")           # not a guard cut, not a normal run
        self.assertEqual(m.analyze_tc30a(tc30a_log(cut=300000))[0], "FAIL")           # the guard never cut anything

    def test_a_cut_run_that_started_before_the_hook_line_is_the_normal_case(self):
        self.assertEqual(m.analyze_tc30a(tc30a_log(cut_started_before_hook=True))[0], "PASS")

    def test_a_long_run_that_ended_before_the_guard_fault_is_not_the_cut(self):
        self.assertEqual(m.analyze_tc30a(tc30a_log(cut_ended_before_hook=True))[0], "FAIL")

    def test_another_long_run_fails(self):
        self.assertEqual(m.analyze_tc30a(tc30a_log(extra_long=True))[0], "FAIL")

    def test_pattern_must_run_before_and_resume_after(self):
        self.assertEqual(m.analyze_tc30a(tc30a_log(n_after=2))[0], "FAIL")

    def test_no_hooks_or_no_motor_is_blocked(self):
        self.assertEqual(m.analyze_tc30a("\n".join(pls(1000, 3)[0]))[0], "BLOCKED")


def tc30b_log(codes=("WDT_RESET", "NVS_READ", "SCAN_STALL"), stray=False, no_boot=False, rearm=True, long_on=False):
    kinds = ["PPE_HOOK fault wdt: a task stopped feeding the watchdog, reset in about 5000 ms t_ms=100",
             "PPE_HOOK fault nvs-read: restarting, the next start takes the fault path (one shot) t_ms=200",
             "PPE_HOOK stall 3 t_ms=300 scan stopped"]
    L = ["PARAM case=tc30b faults=wdt,nvs-read,stall"]
    t = 1000000
    for i in range(3):
        p, t = pls(t, 5); L += p
        L.append(kinds[i])
        if i == 0 and no_boot:
            continue
        L += ["BOOT reset_reason=3", boot_line(343 + i), fault_line(codes[i], 343 + i)]
        if stray and i == 1:
            L.append("E %d 0 1" % t); L.append("E %d 0 0" % (t + 300000))
        if rearm: L.append("STEP rearm %d" % (i + 1))
        t += 8000000
    if long_on:
        L += ["E %d 0 1" % t, "E %d 0 0" % (t + 450000)]
    return "\n".join(L)


class Tc30b(unittest.TestCase):
    def test_pass(self):
        v, msg = m.analyze_tc30b(tc30b_log())
        self.assertEqual(v, "PASS", msg)

    def test_wrong_code_fails(self):
        self.assertEqual(m.analyze_tc30b(tc30b_log(codes=("WDT_RESET", "NVS_WRITE", "SCAN_STALL")))[0], "FAIL")
        self.assertEqual(m.analyze_tc30b(tc30b_log(codes=("SCAN_STALL", "NVS_READ", "SCAN_STALL")))[0], "FAIL")

    def test_no_reboot_fails(self):
        self.assertEqual(m.analyze_tc30b(tc30b_log(no_boot=True))[0], "FAIL")

    def test_motor_on_after_a_reboot_before_rearm_fails(self):
        self.assertEqual(m.analyze_tc30b(tc30b_log(stray=True))[0], "FAIL")

    def test_on_longer_than_400_ms_fails(self):
        self.assertEqual(m.analyze_tc30b(tc30b_log(long_on=True))[0], "FAIL")

    def test_missing_rearm_note_is_blocked(self):
        self.assertEqual(m.analyze_tc30b(tc30b_log(rearm=False))[0], "BLOCKED")


def tc30c_log(no_key_ms=343, periods_before=6, periods_after=2, on=100000, off=100000, low=1800000, alert_pulse=False,
              offline=True, final_alert="1", final_accept="0", key_line="1", cleanup=True, early_rise=False):
    L = ["PARAM case=tc30c timeout_s=780", "PPE_HOOK fault key: restarting, the next start takes the fault path (one shot) t_ms=1",
         "HUB boot (S3 receive path)", boot_line(), fault_line("NO_KEY", no_key_ms)]
    t = 2000000
    p, t = keypulses(t, periods_before, on, off, low)
    if early_rise:                     # bench 1c18cc6: the recorder line of the first rise came before the hub's HUB_BOOT line
        L.insert(3, p.pop(0))
    L += p
    if offline: L.append("MSG msg_type=NODE_OFFLINE vest_id=1 node_id=2 state=OFFLINE last_seen_age_s=780 t_ms=780500")
    p, t = keypulses(t, periods_after, on, off, low); L += p
    if alert_pulse:
        L += ["E %d 0 1" % t, "E %d 0 0" % (t + 300000)]
    L.append("DUMP hub vest=0x01 accept_enabled=%s registry_ok=1 vest_state=NON_COMPLIANT alert_active=%s heartbeat_s=360 margin_s=60 offline_timeout_s=780 msg_pending=0 msg_dropped=0" % (final_accept, final_alert))
    L.append("MOTOR pin=5 state=K_ON out=1 alert=0 key_fault=%s pulse=3 guard_trips=0 max_on_us=100000 t_ms=9" % key_line)
    if cleanup:
        L += ["STEP cleanup: fault wdt returns the hub to a normal start", "PPE_HOOK fault wdt: x t_ms=5", boot_line(367, 6)]
    return "\n".join(L)


class Tc30c(unittest.TestCase):
    def test_pass(self):
        v, msg = m.analyze_tc30c(tc30c_log())
        self.assertEqual(v, "PASS", msg)

    def test_no_key_message_within_2_s(self):
        self.assertEqual(m.analyze_tc30c(tc30c_log(no_key_ms=1999))[0], "PASS")
        self.assertEqual(m.analyze_tc30c(tc30c_log(no_key_ms=2100))[0], "FAIL")

    def test_pattern_tolerances(self):
        self.assertEqual(m.analyze_tc30c(tc30c_log(on=121000))[0], "FAIL")
        self.assertEqual(m.analyze_tc30c(tc30c_log(on=119000, off=119000))[0], "PASS")
        self.assertEqual(m.analyze_tc30c(tc30c_log(off=121000))[0], "FAIL")
        self.assertEqual(m.analyze_tc30c(tc30c_log(low=1950000))[0], "FAIL")           # low time 1800 +/- 100 (D5)
        self.assertEqual(m.analyze_tc30c(tc30c_log(low=1705000))[0], "PASS")
        self.assertEqual(m.analyze_tc30c(tc30c_log(low=1690000))[0], "FAIL")

    def test_alert_pulse_after_offline_fails(self):
        self.assertEqual(m.analyze_tc30c(tc30c_log(alert_pulse=True))[0], "FAIL")

    def test_too_few_periods_or_no_offline_is_blocked_or_fails(self):
        self.assertEqual(m.analyze_tc30c(tc30c_log(periods_before=3))[0], "FAIL")
        self.assertEqual(m.analyze_tc30c(tc30c_log(offline=False))[0], "BLOCKED")
        self.assertEqual(m.analyze_tc30c(tc30c_log(periods_after=0))[0], "BLOCKED")

    def test_final_dump_and_motor_line_are_checked(self):
        self.assertEqual(m.analyze_tc30c(tc30c_log(final_alert="0"))[0], "FAIL")
        self.assertEqual(m.analyze_tc30c(tc30c_log(final_accept="1"))[0], "FAIL")
        self.assertEqual(m.analyze_tc30c(tc30c_log(key_line="0"))[0], "FAIL")

    def test_the_cleanup_reboot_is_not_part_of_the_result(self):
        self.assertEqual(m.analyze_tc30c(tc30c_log(cleanup=True))[0], "PASS")
        self.assertEqual(m.analyze_tc30c(tc30c_log(cleanup=False))[0], "PASS")

    def test_first_rise_logged_before_hub_boot_still_counts(self):
        v, msg = m.analyze_tc30c(tc30c_log(early_rise=True))
        self.assertEqual(v, "PASS", msg)


def tc30d_log(first_off=780400, spread=0, nodes=(1, 2, 3, 4, 5), accept="0", reg="0", acc_after=False, early_motor=False, alert_runs=8,
              fault="REGISTRY", cleanup=True, alert_on=300000):
    L = ["PARAM case=tc30d timeout_s=780", "PPE_HOOK fault registry: restarting, the next start takes the fault path (one shot) t_ms=1",
         boot_line(341), fault_line(fault, 341),
         "DUMP hub vest=0x01 accept_enabled=%s registry_ok=%s vest_state=NON_COMPLIANT alert_active=0 heartbeat_s=360 margin_s=60 offline_timeout_s=780 msg_pending=0 msg_dropped=0" % (accept, reg)]
    if acc_after:
        L.append("ACC t_ms=5000 t_us=5000000 node=3 ctr=5 state=1 batt=90 flags=0x60 missed=0 prev=UNKNOWN now=WORN")
    if early_motor:
        L += ["E 1000000 0 1", "E 1300000 0 0"]
    for i, n in enumerate(nodes):
        L.append("MSG msg_type=NODE_OFFLINE vest_id=1 node_id=%d state=OFFLINE last_seen_age_s=780 t_ms=%d" % (n, first_off + 341 + i * spread))
    p, _ = pls(900000000, alert_runs, on=alert_on); L += p
    if cleanup:
        L += ["STEP cleanup: fault wdt returns the hub to a normal start", boot_line(367, 6), fault_line("WDT_RESET", 367)]
    return "\n".join(L)


class Tc30d(unittest.TestCase):
    def test_pass(self):
        v, msg = m.analyze_tc30d(tc30d_log())
        self.assertEqual(v, "PASS", msg)

    def test_offline_window(self):
        self.assertEqual(m.analyze_tc30d(tc30d_log(first_off=779900))[0], "FAIL")
        self.assertEqual(m.analyze_tc30d(tc30d_log(first_off=785100))[0], "FAIL")
        self.assertEqual(m.analyze_tc30d(tc30d_log(first_off=780000))[0], "PASS")
        self.assertEqual(m.analyze_tc30d(tc30d_log(first_off=780500, spread=1000))[0], "PASS")      # last node at 784.5 s
        self.assertEqual(m.analyze_tc30d(tc30d_log(first_off=780500, spread=1300))[0], "FAIL")     # last node at 785.7 s

    def test_all_five_nodes_needed(self):
        self.assertEqual(m.analyze_tc30d(tc30d_log(nodes=(1, 2, 3, 4)))[0], "FAIL")

    def test_wrong_code_or_enabled_accept_fails(self):
        self.assertEqual(m.analyze_tc30d(tc30d_log(fault="NO_KEY"))[0], "FAIL")
        self.assertEqual(m.analyze_tc30d(tc30d_log(accept="1"))[0], "FAIL")
        self.assertEqual(m.analyze_tc30d(tc30d_log(reg="1"))[0], "FAIL")

    def test_an_accepted_packet_fails(self):
        self.assertEqual(m.analyze_tc30d(tc30d_log(acc_after=True))[0], "FAIL")

    def test_motor_before_offline_or_wrong_pattern_fails(self):
        self.assertEqual(m.analyze_tc30d(tc30d_log(early_motor=True))[0], "FAIL")
        self.assertEqual(m.analyze_tc30d(tc30d_log(alert_runs=3))[0], "FAIL")
        self.assertEqual(m.analyze_tc30d(tc30d_log(alert_on=100000))[0], "FAIL")

    def test_cleanup_is_ignored(self):
        self.assertEqual(m.analyze_tc30d(tc30d_log(cleanup=False))[0], "PASS")


class Tc30Dispatch(unittest.TestCase):
    def test_analyze_text_dispatch(self):
        self.assertEqual(m.analyze_text(tc30a_log() + "\nPARAM case=tc30a")[0], "PASS")
        self.assertEqual(m.analyze_text(tc30b_log())[0], "PASS")
        self.assertEqual(m.analyze_text(tc30c_log())[0], "PASS")
        self.assertEqual(m.analyze_text(tc30d_log())[0], "PASS")


def wdton_log(cuts=5, cut_us=150000, extra_on=None):
    """BUG-007 regression log: per repeat 3 normal alert pulses, then one ON cut by the watchdog and a HUB_BOOT."""
    L = ["PARAM case=wdton repeats=%d need=5" % cuts]
    t = 1000000
    for _ in range(cuts):
        for _ in range(3):
            L += ["E %d 0 1" % t, "E %d 0 0" % (t + 300000)]
            t += 1000000
        L += ["E %d 0 1" % t, "E %d 0 0" % (t + cut_us), "Rebooting...", "HUB boot (S3 receive path)", boot_line(367, 6)]
        t += 5000000
    if extra_on:
        L += ["E %d 0 1" % t, "E %d 0 0" % (t + extra_on)]
    return "\n".join(L)


class Wdton(unittest.TestCase):
    def test_pass(self):
        v, msg = m.analyze_wdton(wdton_log())
        self.assertEqual(v, "PASS", msg)
        self.assertEqual(m.analyze_text(wdton_log())[0], "PASS")

    def test_too_few_cuts_is_blocked_not_pass(self):
        self.assertEqual(m.analyze_wdton(wdton_log(cuts=4))[0], "BLOCKED")

    def test_long_on_fails(self):
        self.assertEqual(m.analyze_wdton(wdton_log(cut_us=401000))[0], "FAIL")
        self.assertEqual(m.analyze_wdton(wdton_log(extra_on=538200))[0], "FAIL")

    def test_cut_near_400_passes(self):
        self.assertEqual(m.analyze_wdton(wdton_log(cut_us=330000))[0], "PASS")

    def test_normal_pulses_are_not_counted_as_cuts(self):
        self.assertEqual(m.analyze_wdton(wdton_log(cut_us=300500))[0], "BLOCKED")


if __name__ == "__main__":
    unittest.main()
