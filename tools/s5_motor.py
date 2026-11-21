#!/usr/bin/env python3
"""S5 motor pin edge recording and analysis (TC-29, TC-30, TC-74 support). TEST TOOL.

Alternative equipment (decision D8): no logic analyzer. A spare ESP32-C3 running tools/edge_recorder records the edges
of the hub motor pin (channel 0, GPIO 4 <- hub GPIO 5 before the driver) and of the hub marker pin (channel 1, GPIO 3 <- hub
GPIO 6) with its own 1 MHz clock. The recorder is calibrated first against a known pulse train that the hub generates
on the marker pin (hub hook `pulse`); the measured error is reported in the log.

  python tools/s5_motor.py cal --hub COM3 --rec COM8 --rehearsal      # 21 s, scratch log
  python tools/s5_motor.py cal --hub COM3 --rec COM8                  # evidence log test_logs/S5/CAL-recorder_<sha7>.log
  python tools/s5_motor.py analyze --log <log>                        # re-run the analyzer, no hardware

  python tools/s5_motor.py tc29a --tc TC-29a --hub COM3 --rec COM8    # steps 1-3: start latency, 60 s pattern, stop (about 85 s)
  python tools/s5_motor.py tc29b --tc TC-29b --hub COM3 --rec COM8    # step 4: reset with all nodes UNKNOWN, no motor activity
  python tools/s5_motor.py tc29c --tc TC-29c --hub COM3 --rec COM8    # step 5: OFFLINE trigger (about 13 min at the defaults)
  python tools/s5_motor.py result --tc TC-29a --result PASS --note "..."   # only after a PASS verdict

  python tools/s5_motor.py tc30a --tc TC-30a --hub COM3 --rec COM8    # nvs-write and guard faults while the alert runs (about 45 s)
  python tools/s5_motor.py tc30b --tc TC-30b --hub COM3 --rec COM8    # reboot faults: wdt, nvs-read, scan stall (about 3 min)
  python tools/s5_motor.py tc30c --tc TC-30c --hub COM3 --rec COM8    # missing key: NO_KEY, KEY_FAULT pattern (about 13.5 min at the defaults)
  python tools/s5_motor.py tc30d --tc TC-30d --hub COM3 --rec COM8    # unreadable registry: REGISTRY, OFFLINE, alert (about 13.5 min)

Wiring for the calibration: recorder GPIO 3 <- hub GPIO 6, recorder GND <- hub GND (nothing else on GPIO 6).
Wiring for TC-29: recorder GPIO 4 <- hub GPIO 5 (the node BEFORE the base resistor), recorder GPIO 3 <- hub GPIO 6 (marker), GND common.
Timing is measured from the accepted packet (hub marker pin, hub hook `marker on`) to the motor pin: alternative equipment, the
on-air TX marker is not available while the only spare C3 is the recorder (recorded as a deviation).
Lines in the log: 'E <t_us> <ch> <lvl>' from the recorder (its clock, 32 bit wrap), everything else from the hub.
"""
import argparse
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import logger  # noqa: E402
import s5_bench as bench  # noqa: E402

CAL_TOL_US = 1000                      # the limits to verify are +/- 20 to 30 ms: 1 ms is more than enough resolution
RE_EDGE = re.compile(bench.PFX + r"E (\d+) ([01]) ([01])\s*$")


def last_ready(text):
    """Index of the recorder's last READY line (the reset that starts the run), -1 when the log has none."""
    return max((i for i, l in enumerate(text.splitlines()) if l.rstrip("\r").strip().endswith("READY")), default=-1)


def edges(text):
    """Recorder edges of THIS run: everything printed before the last READY is stale backlog from before the reset and ignored."""
    out = []
    start = last_ready(text)
    for idx, line in enumerate(text.splitlines()):
        if idx <= start:
            continue
        m = RE_EDGE.match(line.rstrip("\r"))
        if m:
            out.append(dict(idx=idx, t=int(m.group(1)), ch=int(m.group(2)), lvl=int(m.group(3)), pc=bench.pc_seconds(line)))
    return out


def wrap(later, earlier):
    return (later - earlier) & 0xFFFFFFFF


NOISE_US = 200          # edges of one channel closer than this are not a signal (the shortest real pulse is the 1 ms marker)


def noisy(text):
    """[(channel, number of edge pairs closer than NOISE_US)] for the channels that look floating or badly connected."""
    out = []
    for ch in (0, 1):
        ev = [e for e in edges(text) if e["ch"] == ch]
        n = sum(1 for a, b in zip(ev, ev[1:]) if wrap(b["t"], a["t"]) < NOISE_US)
        if n:
            out.append((ch, n))
    return out


def noise_msg(found):
    names = {0: "motor channel (recorder GPIO 4 <- hub GPIO 5)", 1: "marker channel (recorder GPIO 3 <- hub GPIO 6)"}
    return "; ".join("%s is noisy: %d edge pairs closer than %d us (floating input or loose wire)" % (names[c], n, NOISE_US) for c, n in found)


def pairs(ev):
    """Consecutive edges of one channel as (level of the run, duration in us, missed_edge flag)."""
    return [(a["lvl"], wrap(b["t"], a["t"]), a["lvl"] == b["lvl"]) for a, b in zip(ev, ev[1:])]


def analyze_cal(text, on_ms, off_ms, count, ch=1):
    ev = [e for e in edges(text) if e["ch"] == ch]
    if not ev:
        return "BLOCKED", "no edges from the recorder on channel %d: wiring or recorder problem (setup failure)" % ch
    if noisy(text):
        return "BLOCKED", noise_msg(noisy(text))
    runs = pairs(ev)
    bad = sum(1 for r in runs if r[2])
    on = [d for lvl, d, _ in runs if lvl == 1]
    off = [d for lvl, d, _ in runs if lvl == 0]
    if len(ev) != 2 * count or ev[0]["lvl"] != 1 or bad or len(on) != count or len(off) != count - 1:
        return "FAIL", "expected %d clean pulses, recorder saw %d edges (first level %d, %d repeated levels)" % (count, len(ev), ev[0]["lvl"], bad)
    e_on = [d - on_ms * 1000 for d in on]
    e_off = [d - off_ms * 1000 for d in off]
    worst = max(abs(x) for x in e_on + e_off)
    msg = ("%d pulses %d/%d ms: ON error mean %+.0f us max %d us, OFF error mean %+.0f us max %d us; worst case %d us (limit %d us)" % (
        count, on_ms, off_ms, sum(e_on) / len(e_on), max(abs(x) for x in e_on), sum(e_off) / max(1, len(e_off)),
        max([abs(x) for x in e_off] or [0]), worst, CAL_TOL_US))
    return ("PASS" if worst <= CAL_TOL_US else "FAIL"), msg


HALF = 0x80000000
ON_NOM, OFF_NOM, PATTERN_TOL = 300000, 700000, 30000
START_MAX_US, STOP_MAX_US = 200000, 1000000


def after(t, ref):
    return wrap(t, ref) < HALF


def motor_edges(text):
    return [e for e in edges(text) if e["ch"] == 0]


def marker_rises(text):
    return [e for e in edges(text) if e["ch"] == 1 and e["lvl"] == 1]


def pair_counts(text):
    lines = text.splitlines()
    on_idx = [i for i, l in enumerate(lines) if "PPE_HOOK marker on ok" in l]
    accs = [e for e in bench.events(text) if e["kind"] == "acc" and on_idx and e["idx"] > on_idx[0]]
    return len(accs), len(marker_rises(text))


def pair_acc_markers(text):
    """The i-th accepted packet after `marker on` and the i-th marker pulse belong together (one pulse per ACC). None on a count mismatch."""
    lines = text.splitlines()
    on_idx = [i for i, l in enumerate(lines) if "PPE_HOOK marker on ok" in l]
    if not on_idx:
        return None
    accs = [e for e in bench.events(text) if e["kind"] == "acc" and e["idx"] > on_idx[0]]
    rises = marker_rises(text)
    if len(accs) != len(rises):
        return None
    return list(zip(accs, rises))


def find_marker(pairs, node, ctr):
    for a, r in pairs:
        if a["node"] == node and a["ctr"] == ctr:
            return a, r
    return None


def analyze_tc29a(text, node, ctr_nw, ctr_w):
    """Steps 1-3: start <= 200 ms after the accepted NOT_WORN packet, ON 300 +/- 30 and OFF 700 +/- 30 over ~60 s, stop <= 1 s."""
    if noisy(text):
        return "BLOCKED", noise_msg(noisy(text))
    pr = pair_acc_markers(text)
    if pr is None:
        return "BLOCKED", "%d accepted packets but %d marker pulses: they do not pair up (marker wiring or `marker on` missing: setup failure)" % pair_counts(text)
    m1, m2 = find_marker(pr, node, ctr_nw), find_marker(pr, node, ctr_w)
    if not m1 or not m2:
        return "BLOCKED", "the NOT_WORN or the WORN packet was not accepted (setup failure)"
    t1, t2 = m1[1]["t"], m2[1]["t"]
    me = motor_edges(text)
    before = [e for e in me if not after(e["t"], t1)]
    if before and (before[-1]["lvl"] != 0 or wrap(t1, before[-1]["t"]) < 1500000):
        return "BLOCKED", "the motor was not idle when the NOT_WORN packet arrived (an earlier alert was still running or ending)"
    run = [e for e in me if after(e["t"], t1) and not after(e["t"], t2)]
    if not run or run[0]["lvl"] != 1:
        return "FAIL", "the motor never started after the NOT_WORN packet"
    start = wrap(run[0]["t"], t1)
    on = [wrap(b["t"], a["t"]) for a, b in zip(run, run[1:]) if a["lvl"] == 1 and b["lvl"] == 0]
    off = [wrap(b["t"], a["t"]) for a, b in zip(run, run[1:]) if a["lvl"] == 0 and b["lvl"] == 1]
    rep_ = sum(1 for a, b in zip(run, run[1:]) if a["lvl"] == b["lvl"])
    span = wrap(t2, t1)
    msg = "start %.1f ms after the accepted packet; %d ON runs %.1f..%.1f ms, %d OFF runs %.1f..%.1f ms over %.1f s" % (
        start / 1000.0, len(on), min(on or [0]) / 1000.0, max(on or [0]) / 1000.0, len(off), min(off or [0]) / 1000.0, max(off or [0]) / 1000.0, span / 1e6)
    if start > START_MAX_US:
        return "FAIL", msg + "; start later than 200 ms"
    if rep_ or len(on) < 55 or len(off) < 54 or span < 58000000:
        return "FAIL", msg + "; fewer than 55 clean cycles in about 60 s (or a missed edge)"
    if any(abs(d - ON_NOM) > PATTERN_TOL for d in on) or any(abs(d - OFF_NOM) > PATTERN_TOL for d in off):
        return "FAIL", msg + "; a run is outside 300 +/- 30 / 700 +/- 30 ms"
    post = [e for e in me if after(e["t"], t2)]
    if post:
        if post[0]["lvl"] != 0:
            return "FAIL", msg + "; the motor started again after the WORN packet"
        stop = wrap(post[0]["t"], t2)
        if len(post) > 1:
            return "FAIL", msg + "; more motor edges after the stop"
    else:
        if not run or run[-1]["lvl"] != 0:
            return "FAIL", msg + "; the motor stayed on after the WORN packet"
        stop = 0
    if stop > STOP_MAX_US:
        return "FAIL", msg + "; stop %.1f ms after the WORN packet (limit 1000 ms)" % (stop / 1000.0)
    return "PASS", msg + "; stop %.1f ms after the WORN packet%s" % (stop / 1000.0, " (already low)" if not post else "")


def analyze_tc29b(text):
    """Step 4: a hub reset with all nodes UNKNOWN: no motor edge from the reset until 60 s after HUB_BOOT."""
    ev = bench.events(text)
    boots = [e for e in ev if e["kind"] == "boot"]
    if len(boots) != 1:
        return "BLOCKED", "%d HUB_BOOT lines (the hub must be reset exactly once)" % len(boots)
    after_boot = [e for e in ev if e["kind"] == "dump" and e["idx"] > boots[0]["idx"]]
    if not after_boot:
        return "BLOCKED", "no dump after the boot"
    d = after_boot[0]
    if any(n["state"] != "UNKNOWN" for n in d["nodes"].values()) or len(d["nodes"]) != 5 or d["hub"].get("alert") != 0:
        return "FAIL", "after the reset not all nodes are UNKNOWN or alert_active is not 0"
    lines = text.splitlines()
    ready = [i for i, l in enumerate(lines) if l.strip().endswith("READY")]
    if not ready:
        return "BLOCKED", "no READY from the recorder: the recording did not start before the reset"
    obs = bench.params(text).get("observe_s")
    if not obs or int(obs) < 60:
        return "BLOCKED", "the run did not observe 60 s after HUB_BOOT"
    # The marker pin (hub GPIO 6) is undriven for a moment after every reset and is not used here: its channel is ignored.
    # Edges on the MOTOR channel are the result, never a setup error: a pin that floats during the reset is the B-01 finding.
    me = motor_edges(text)
    if me:
        close = sum(1 for a, b in zip(me, me[1:]) if wrap(b["t"], a["t"]) < NOISE_US)
        hint = ("; %d edge pairs only microseconds apart: the motor pin floats during the reset (B-01: the external pull-down is missing or not working)" % close) if close else ""
        return "FAIL", "%d motor edges between the reset and %s s after boot (first: level %d)%s" % (len(me), obs, me[0]["lvl"], hint)
    return "PASS", "no motor edge from the reset to %s s after HUB_BOOT, all nodes UNKNOWN, alert_active=0" % obs


def analyze_tc29c(text, node, ctr, timeout_s):
    """Step 5: OFFLINE trigger: the motor starts <= 200 ms after the node goes OFFLINE (hub clock mapped to the recorder clock
    through the marker pulse of the node's last accepted packet) and not before."""
    if noisy(text):
        return "BLOCKED", noise_msg(noisy(text))
    pr = pair_acc_markers(text)
    if pr is None:
        return "BLOCKED", "%d accepted packets but %d marker pulses: they do not pair up (setup failure)" % pair_counts(text)
    m = find_marker(pr, node, ctr)
    if not m:
        return "BLOCKED", "the last packet of node %d was not accepted" % node
    acc, rise = m
    offs = [e for e in bench.events(text) if e["kind"] == "off" and e["node"] == node and e["idx"] > acc["idx"]]
    if not offs:
        return "FAIL", "no NODE_OFFLINE for node %d" % node
    mask = 0xFFFFFFFF
    t_off = (offs[0]["t"] * 1000 + wrap(rise["t"], acc["t"] * 1000)) & mask      # the hub transition time on the recorder clock
    t_cut = (t_off - 50000) & mask                                                # clock mapping uncertainty: a few ms (marker delay, drift)
    settle = (rise["t"] + 1500000) & mask                                         # the packet may still end an earlier alert for 1.5 s
    rises = [e for e in motor_edges(text) if e["lvl"] == 1]
    gap = wrap(offs[0]["t"], acc["t"])
    base = "NODE_OFFLINE %.3f s after the last packet (timeout %d s)" % (gap / 1000.0, timeout_s)
    early = [e for e in rises if after(e["t"], settle) and not after(e["t"], t_cut)]
    if early:
        return "FAIL", base + "; the motor started %.1f s before the node went OFFLINE" % (wrap(t_off, early[0]["t"]) / 1e6)
    start = [e for e in rises if after(e["t"], t_cut)]
    if not start:
        return "FAIL", base + "; the motor never started after NODE_OFFLINE"
    lat = (start[0]["t"] - t_off) if after(start[0]["t"], t_off) else -wrap(t_off, start[0]["t"])
    msg = base + "; motor started %+.1f ms from the OFFLINE transition (clock mapping uncertainty a few ms)" % (lat / 1000.0)
    if lat > START_MAX_US:
        return "FAIL", msg + "; later than 200 ms"
    return "PASS", msg


# ---------------- TC-30: hub fault handling ------------------------------------------------------------------
RE_FAULT = re.compile(bench.PFX + r"MSG msg_type=HUB_FAULT vest_id=\d+ code=(\w+) t_ms=(\d+)")
RE_FHOOK = re.compile(bench.PFX + r"PPE_HOOK (?:fault ([a-z-]+)|(stall) \d+)\b.* t_ms=(\d+)")
RE_DHUBF = re.compile(bench.PFX + r"DUMP hub vest=\S+ accept_enabled=(\d) registry_ok=(\d) vest_state=\w+ alert_active=(\d)")
RE_MOTORL = re.compile(bench.PFX + r"MOTOR pin=\d+ state=(\w+) out=\d alert=(\d) key_fault=(\d)")
ON_MAX_US = 400000
KEY_ON, KEY_OFF, KEY_LOW3, KEY_TOL, KEY_LOW_TOL = 100000, 100000, 1800000, 20000, 100000


def cut_cleanup(text):
    out = []
    for l in text.splitlines():
        if "STEP cleanup" in l:
            break
        out.append(l)
    return "\n".join(out)


def faults_in(text):
    return [dict(idx=i, code=m.group(1), t=int(m.group(2))) for i, l in enumerate(text.splitlines()) for m in [RE_FAULT.match(l.rstrip("\r"))] if m]


def fault_hooks(text):
    out = []
    for i, l in enumerate(text.splitlines()):
        m = RE_FHOOK.match(l.rstrip("\r"))
        if m:
            out.append(dict(idx=i, kind=m.group(1) or "stall", t=int(m.group(3))))
    return out


def on_runs(me):
    """ON runs of the motor channel: dict(idx of the rise, t of the rise, duration in us)."""
    return [dict(idx=a["idx"], fall_idx=b["idx"], t=a["t"], dur=wrap(b["t"], a["t"])) for a, b in zip(me, me[1:]) if a["lvl"] == 1 and b["lvl"] == 0]


def alert_runs_ok(runs):
    return all(abs(r["dur"] - ON_NOM) <= PATTERN_TOL for r in runs)


def analyze_tc30a(text):
    """fault nvs-write and fault guard while the alert runs: the right HUB_FAULT codes, no ON above 400 ms, the guard cut at about
    395 ms, the pattern resumes."""
    if noisy(text):
        return "BLOCKED", noise_msg(noisy(text))
    hooks = {h["kind"]: h for h in fault_hooks(text)}
    if "nvs-write" not in hooks or "guard" not in hooks:
        return "BLOCKED", "the fault hooks were not accepted (flash the S5 part 2e build)"
    fl = faults_in(text)
    nw = [f for f in fl if f["code"] == "NVS_WRITE" and f["idx"] > hooks["nvs-write"]["idx"]]
    gd = [f for f in fl if f["code"] == "GUARD" and f["idx"] > hooks["guard"]["idx"]]
    if not nw or wrap(nw[0]["t"], hooks["nvs-write"]["t"]) > 3000:
        return "FAIL", "no HUB_FAULT NVS_WRITE within 3 s of the nvs-write fault"
    if not gd or wrap(gd[0]["t"], hooks["guard"]["t"]) > 4000:
        return "FAIL", "no HUB_FAULT GUARD within 4 s of the guard fault"
    runs = on_runs(motor_edges(text))
    if not runs:
        return "BLOCKED", "no motor activity: the alert was not running during the faults"
    longest = max(r["dur"] for r in runs)
    msg = "%d ON runs, longest %.1f ms; NVS_WRITE %d ms and GUARD %d ms after their hooks" % (
        len(runs), longest / 1000.0, wrap(nw[0]["t"], hooks["nvs-write"]["t"]), wrap(gd[0]["t"], hooks["guard"]["t"]))
    if longest > ON_MAX_US:
        return "FAIL", msg + "; an ON run is longer than 400 ms"
    cut = [r for r in runs if r["dur"] > ON_NOM + PATTERN_TOL]
    if len(cut) != 1 or not (380000 <= cut[0]["dur"] <= ON_MAX_US) or cut[0]["fall_idx"] < hooks["guard"]["idx"]:
        # the fault arms as soon as the motor is ON, so the cut run may have started before the hook line: it must END after it
        return "FAIL", msg + "; expected exactly one guard-cut ON run of 380..400 ms ending after the guard fault (found %d long runs)" % len(cut)
    normal = [r for r in runs if r is not cut[0]]
    if not alert_runs_ok(normal):
        return "FAIL", msg + "; an ON run outside 300 +/- 30 ms"
    if sum(1 for r in normal if r["idx"] < cut[0]["idx"]) < 3 or sum(1 for r in normal if r["idx"] > cut[0]["idx"]) < 3:
        return "FAIL", msg + "; the pattern did not run before and resume after the guard cut (3 normal runs each side)"
    return "PASS", msg + "; guard cut %.1f ms, pattern resumed" % (cut[0]["dur"] / 1000.0)


RESET_FAULT = {"wdt": "WDT_RESET", "nvs-read": "NVS_READ", "stall": "SCAN_STALL"}


def analyze_tc30b(text):
    """Reboot faults (wdt, nvs-read, scan stall) with the alert running: HUB_BOOT, then the right HUB_FAULT code; no ON above 400 ms;
    the motor stays quiet after every reboot until the alert is armed again."""
    p = bench.params(text)
    kinds = [k for k in p.get("faults", "").split(",") if k]
    if not kinds:
        return "BLOCKED", "no PARAM faults= line"
    if noisy(text):
        return "BLOCKED", noise_msg(noisy(text))
    lines = text.splitlines()
    hooks = [h for h in fault_hooks(text) if h["kind"] in RESET_FAULT]
    if len(hooks) != len(kinds):
        return "BLOCKED", "%d fault hooks in the log, %d expected" % (len(hooks), len(kinds))
    boots = [e for e in bench.events(text) if e["kind"] == "boot"]
    fl = faults_in(text)
    rearm = [i for i, l in enumerate(lines) if "STEP rearm" in l]
    me = motor_edges(text)
    runs = on_runs(me)
    longest = max([r["dur"] for r in runs] or [0])
    parts = []
    for k, h in enumerate(hooks):
        nxt = hooks[k + 1]["idx"] if k + 1 < len(hooks) else len(lines)
        b = [e for e in boots if h["idx"] < e["idx"] < nxt]
        if not b:
            return "FAIL", "no HUB_BOOT after the %s fault: the hub did not reboot" % h["kind"]
        want = RESET_FAULT[h["kind"]]
        f = [x for x in fl if b[0]["idx"] < x["idx"] < nxt]
        if not f or f[0]["code"] != want:
            return "FAIL", "after the %s fault HUB_BOOT was followed by %s, expected %s" % (h["kind"], f[0]["code"] if f else "no HUB_FAULT", want)
        r = [i for i in rearm if b[0]["idx"] < i < nxt]
        if not r:
            return "BLOCKED", "no re-arm note after the %s reboot (driver problem)" % h["kind"]
        stray = [e for e in me if e["lvl"] == 1 and b[0]["idx"] < e["idx"] < r[0]]
        if stray:
            return "FAIL", "the motor switched on after the %s reboot before the alert was armed again" % h["kind"]
        parts.append("%s -> HUB_BOOT then %s" % (h["kind"], want))
    msg = "; ".join(parts) + "; longest ON %.1f ms" % (longest / 1000.0)
    if not runs:
        return "BLOCKED", "no motor activity: the alert was not running before the faults"
    if longest > ON_MAX_US:
        return "FAIL", msg + " (an ON run is longer than 400 ms)"
    return "PASS", msg


def key_pattern(me):
    """(ok, message) for the KEY_FAULT pattern in the given edges: ON 100 +/- 20, OFF 100 +/- 20 (x3), low after the third ON 1800 +/- 100."""
    runs = on_runs(me)
    if len(runs) < 15:
        return False, "only %d pulses recorded (need 5 periods = 15)" % len(runs)
    bad = [r for r in runs if abs(r["dur"] - KEY_ON) > KEY_TOL]
    if bad:
        return False, "a KEY_FAULT pulse is %.1f ms (limit 100 +/- 20)" % (bad[0]["dur"] / 1000.0)
    offs = []
    rises = [r["t"] for r in runs]
    for i in range(len(runs) - 1):
        offs.append(wrap(rises[i + 1], rises[i]) - runs[i]["dur"])
    worst = ""
    for i, o in enumerate(offs):
        if i % 3 == 2:
            if abs(o - KEY_LOW3) > KEY_LOW_TOL:
                return False, "the low time after pulse %d is %.1f ms (limit 1800 +/- 100)" % (i + 1, o / 1000.0)
        elif abs(o - KEY_OFF) > KEY_TOL:
            return False, "an OFF between pulses is %.1f ms (limit 100 +/- 20)" % (o / 1000.0)
    lows = [o for i, o in enumerate(offs) if i % 3 == 2]
    ons_in = [o for i, o in enumerate(offs) if i % 3 != 2]
    return True, "%d pulses: ON %.1f..%.1f ms, OFF %.1f..%.1f ms, low after the third ON %.1f..%.1f ms" % (
        len(runs), min(r["dur"] for r in runs) / 1000.0, max(r["dur"] for r in runs) / 1000.0, min(ons_in) / 1000.0, max(ons_in) / 1000.0,
        min(lows) / 1000.0, max(lows) / 1000.0)


def analyze_tc30c(text):
    """Missing key: HUB_FAULT NO_KEY within 2 s of boot, the KEY_FAULT pattern, never the alert pattern, also after the nodes went OFFLINE."""
    text = cut_cleanup(text)
    if noisy(text):
        return "BLOCKED", noise_msg(noisy(text))
    hooks = [h for h in fault_hooks(text) if h["kind"] == "key"]
    boots = [e for e in bench.events(text) if e["kind"] == "boot"]
    if not hooks or not boots:
        return "BLOCKED", "no fault key hook or no HUB_BOOT in the log"
    b = [e for e in boots if e["idx"] > hooks[0]["idx"]]
    if not b:
        return "FAIL", "the hub did not reboot after fault key"
    f = [x for x in faults_in(text) if x["idx"] > b[0]["idx"]]
    if not f or f[0]["code"] != "NO_KEY":
        return "FAIL", "after fault key HUB_BOOT was followed by %s, expected NO_KEY" % (f[0]["code"] if f else "no HUB_FAULT")
    if f[0]["t"] > 2000:
        return "FAIL", "NO_KEY came %d ms after boot (limit 2000 ms)" % f[0]["t"]
    me = [e for e in motor_edges(text) if e["idx"] > b[0]["idx"]]
    off = [e for e in bench.events(text) if e["kind"] == "off" and e["idx"] > b[0]["idx"]]
    if not off:
        return "BLOCKED", "the nodes never went OFFLINE during the observation (too short)"
    ok, pat = key_pattern([e for e in me if e["idx"] < off[0]["idx"]])
    if not ok:
        return "FAIL", "NO_KEY after %d ms; " % f[0]["t"] + pat
    after_off = on_runs([e for e in me if e["idx"] > off[0]["idx"]])
    if len(after_off) < 3:
        return "BLOCKED", "fewer than 3 pulses recorded after the nodes went OFFLINE"
    if any(abs(r["dur"] - KEY_ON) > KEY_TOL for r in on_runs(me)):
        return "FAIL", "NO_KEY after %d ms; a pulse is not 100 +/- 20 ms: the alert pattern ran while the key fault was active" % f[0]["t"]
    lines = text.splitlines()
    dumps = [RE_DHUBF.match(l.rstrip("\r")) for l in lines]
    dumps = [m for m in dumps if m]
    if not dumps or dumps[-1].group(3) != "1" or dumps[-1].group(1) != "0":
        return "FAIL", "the final dump does not show accept_enabled=0 and alert_active=1 (the alert decision must stay true while KEY_FAULT owns the motor)"
    mot = [m for m in (RE_MOTORL.match(l.rstrip("\r")) for l in lines) if m]
    if not mot or mot[-1].group(3) != "1":
        return "FAIL", "the final MOTOR line does not show key_fault=1"
    return "PASS", "NO_KEY %d ms after boot; %s; %d more pulses after the nodes went OFFLINE, all 100 ms (never the alert pattern)" % (f[0]["t"], pat, len(after_off))


def analyze_tc30d(text):
    """Unreadable registry: HUB_FAULT REGISTRY, accept disabled, every node OFFLINE 780..785 s after boot, the alert pattern runs."""
    text = cut_cleanup(text)
    if noisy(text):
        return "BLOCKED", noise_msg(noisy(text))
    p = bench.params(text)
    timeout_s = int(p.get("timeout_s", "780"))
    hooks = [h for h in fault_hooks(text) if h["kind"] == "registry"]
    boots = [e for e in bench.events(text) if e["kind"] == "boot"]
    if not hooks or not boots:
        return "BLOCKED", "no fault registry hook or no HUB_BOOT in the log"
    b = [e for e in boots if e["idx"] > hooks[0]["idx"]]
    if not b:
        return "FAIL", "the hub did not reboot after fault registry"
    f = [x for x in faults_in(text) if x["idx"] > b[0]["idx"]]
    if not f or f[0]["code"] != "REGISTRY":
        return "FAIL", "after fault registry HUB_BOOT was followed by %s, expected REGISTRY" % (f[0]["code"] if f else "no HUB_FAULT")
    lines = text.splitlines()
    dumps = [m for m in (RE_DHUBF.match(l.rstrip("\r")) for i, l in enumerate(lines) if i > b[0]["idx"]) if m]
    if not dumps or dumps[0].group(1) != "0" or dumps[0].group(2) != "0":
        return "FAIL", "the dump after boot does not show accept_enabled=0 and registry_ok=0"
    if any(e["kind"] == "acc" and e["idx"] > b[0]["idx"] for e in bench.events(text)):
        return "FAIL", "a packet was accepted although the registry is unreadable"
    off = [e for e in bench.events(text) if e["kind"] == "off" and e["idx"] > b[0]["idx"]]
    nodes = {e["node"]: e for e in off}
    if sorted(nodes) != [1, 2, 3, 4, 5]:
        return "FAIL", "only nodes %s went OFFLINE" % sorted(nodes)
    lo, hi = timeout_s * 1000 - 50, timeout_s * 1000 + 5000
    late = [n for n, e in sorted(nodes.items()) if not (lo <= wrap(e["t"], b[0]["t"]) <= hi)]
    if late:
        return "FAIL", "node %s was not OFFLINE %d..%d s after boot (boot t_ms %d, first OFFLINE %d)" % (late[0], timeout_s, timeout_s + 5, b[0]["t"], min(e["t"] for e in off))
    me = [e for e in motor_edges(text) if e["idx"] > b[0]["idx"]]
    first_off_idx = min(e["idx"] for e in off)
    if [e for e in me if e["lvl"] == 1 and e["idx"] < first_off_idx]:
        return "FAIL", "the motor switched on before any node was OFFLINE"
    runs = on_runs([e for e in me if e["idx"] > first_off_idx - 1])
    if len(runs) < 5 or not alert_runs_ok(runs):
        return "FAIL", "after OFFLINE the motor shows %d ON runs, expected the alert pattern (>= 5 runs of 300 +/- 30 ms)" % len(runs)
    return "PASS", "REGISTRY reported, accept disabled, all 5 nodes OFFLINE %.3f..%.3f s after boot, %d alert pattern runs after it" % (
        min(wrap(e["t"], b[0]["t"]) for e in off) / 1000.0, max(wrap(e["t"], b[0]["t"]) for e in off) / 1000.0, len(runs))


def analyze_text(text):
    p = bench.params(text)
    if p.get("case") == "cal":
        return analyze_cal(text, int(p["on_ms"]), int(p["off_ms"]), int(p["count"]))
    if p.get("case") == "tc29a":
        return analyze_tc29a(text, int(p["node"]), int(p["ctr_nw"]), int(p["ctr_w"]))
    if p.get("case") == "tc29b":
        return analyze_tc29b(text)
    if p.get("case") == "tc29c":
        return analyze_tc29c(text, int(p["node"]), int(p["ctr"]), int(p["timeout_s"]))
    if p.get("case") == "tc30a":
        return analyze_tc30a(text)
    if p.get("case") == "tc30b":
        return analyze_tc30b(text)
    if p.get("case") == "tc30c":
        return analyze_tc30c(text)
    if p.get("case") == "tc30d":
        return analyze_tc30d(text)
    return "BLOCKED", "no PARAM line in the log: not a run of this driver"


def run_cal(a):
    a.tc = "CAL-recorder"
    cap, path = bench.open_run(a, ["case=cal recorder=%s" % a.rec])
    sha0 = bench.head_sha()
    on_ms, off_ms, count = 300, 700, 20
    cap.note("EXPECT the recorder measures a known %d/%d ms pulse train (hub hook pulse) within %d us" % (on_ms, off_ms, CAL_TOL_US))
    cap.note("PARAM case=cal on_ms=%d off_ms=%d count=%d" % (on_ms, off_ms, count))
    rec = logger.Capture(a.rec, 115200, path, None)
    time.sleep(0.5)
    rec.send("PING")
    if not rec.wait_for(lambda l: l.strip() == "PONG", 3):
        rec.close()
        return bench.finish(cap, path, a, sha0, "BLOCKED", "the recorder does not answer PING (flash tools/edge_recorder, unplug and replug the C3)")
    rec.send("RESET")
    rec.wait_for(lambda l: l.strip() == "READY", 3)
    cap.send("pulse %d %d %d" % (on_ms, off_ms, count))
    if not cap.wait_for(lambda l: "PPE_HOOK pulse" in l and " ok " in l, 3):
        rec.close()
        return bench.finish(cap, path, a, sha0, "BLOCKED", "the hub did not accept the pulse hook (flash the S5 part 2c build)")
    wait = 0.5 + count * (on_ms + off_ms) / 1000.0 + 1.5
    print("  generating %d pulses, %.0f s ..." % (count, wait))
    time.sleep(wait)
    rec.send("STAT")
    time.sleep(0.5)
    v, msg = analyze_cal("\n".join(rec.lines), on_ms, off_ms, count)
    rec.close()
    return bench.finish(cap, path, a, sha0, v, msg)


def setup_run(a, extra):
    cap, path = bench.open_run(a, extra)
    sha0 = bench.head_sha()
    rec = logger.Capture(a.rec, 115200, path, None)
    time.sleep(0.5)
    rec.send("PING")
    if not rec.wait_for(lambda l: l.strip() == "PONG", 3):
        rec.close()
        return cap, rec, path, sha0, "the recorder does not answer PING (flash tools/edge_recorder, unplug and replug the C3)"
    return cap, rec, path, sha0, None


def quiet_marker(rec, secs=1.5):
    """After RESET the marker input must stay quiet while the hub is idle; returns an error text or None."""
    rec.send("RESET")
    if not rec.wait_for(lambda l: l.strip() == "READY", 3):
        return "the recorder did not answer RESET"
    time.sleep(secs)
    ready = max(i for i, l in enumerate(rec.lines) if l.strip() == "READY")
    stale = sum(1 for l in rec.lines[:ready] if RE_EDGE.match(l))
    if stale:
        rec.note("INFO %d stale recorder edges from before the reset were discarded" % stale)
        print("  %d stale recorder edges from before the reset discarded" % stale)
    n = sum(1 for l in rec.lines[ready + 1:] if RE_EDGE.match(l) and RE_EDGE.match(l).group(2) == "1")
    if n:
        return "the marker input (recorder GPIO 3) shows %d edges while the hub is idle: floating or loose wire to hub GPIO 6" % n
    return None


def combined(cap, rec):
    return "\n".join(cap.lines + rec.lines)


def sleep_note(cap, secs, label):
    end = time.monotonic() + secs
    while time.monotonic() < end:
        cap.note("STEP %s, %.0f s left" % (label, end - time.monotonic()))
        print("  %s, %.0f s left" % (label, end - time.monotonic()))
        time.sleep(min(15, max(0.1, end - time.monotonic())))


def send_pkt(cap, node, ctr, state):
    import inject as inj
    cap.send("inject " + inj.signed(node, ctr, state).hex())


def prime(cap, base):
    for n in range(1, 6):
        send_pkt(cap, n, base + n, 1)
        time.sleep(0.8)


def finish_run(cap, rec, path, a, sha0, v, msg):
    rec.close()
    return bench.finish(cap, path, a, sha0, v, msg)


def run_tc29a(a):
    cap, rec, path, sha0, err = setup_run(a, ["case=tc29a node=2"])
    if err:
        return bench.finish(cap, path, a, sha0, "BLOCKED", err)
    d0 = bench.get_dump(cap)
    if not d0:
        return finish_run(cap, rec, path, a, sha0, "BLOCKED", "no dump from the hub (setup failure)")
    base = max(n["last_counter"] for n in d0["nodes"].values()) + 100
    ctr_nw, ctr_w = base + 20, base + 21
    cap.note("EXPECT start <= 200 ms, ON 300 +/- 30 ms and OFF 700 +/- 30 ms over 60 s, stop <= 1 s")
    cap.note("PARAM case=tc29a node=2 ctr_nw=%d ctr_w=%d" % (ctr_nw, ctr_w))
    cap.send("marker on")
    if not cap.wait_for(lambda l: "PPE_HOOK marker on ok" in l, 3):
        return finish_run(cap, rec, path, a, sha0, "BLOCKED", "the hub did not accept `marker on` (flash the S5 part 2c build)")
    err = quiet_marker(rec)
    if err:
        return finish_run(cap, rec, path, a, sha0, "BLOCKED", err)
    prime(cap, base)
    time.sleep(2.5)
    cap.note("STEP 1: NOT_WORN packet from node 2")
    send_pkt(cap, 2, ctr_nw, 0)
    sleep_note(cap, 61.0, "step 2: recording the pattern")
    cap.note("STEP 3: WORN packet from the last offender")
    send_pkt(cap, 2, ctr_w, 1)
    time.sleep(4.0)
    rec.send("STAT"); time.sleep(0.5)
    cap.send("motor"); time.sleep(0.8)
    v, msg = analyze_tc29a(combined(cap, rec), 2, ctr_nw, ctr_w)
    return finish_run(cap, rec, path, a, sha0, v, msg)


def run_tc29b(a):
    cap, rec, path, sha0, err = setup_run(a, ["case=tc29b"])
    if err:
        return bench.finish(cap, path, a, sha0, "BLOCKED", err)
    d0 = bench.get_dump(cap)
    if not d0:
        return finish_run(cap, rec, path, a, sha0, "BLOCKED", "no dump from the hub (setup failure)")
    base = max(n["last_counter"] for n in d0["nodes"].values()) + 100
    cap.note("EXPECT no motor edge from the reset until 60 s after HUB_BOOT; all nodes UNKNOWN, alert_active=0")
    prime(cap, base)
    time.sleep(1.5)
    cap.note("PARAM case=tc29b observe_s=0")
    rec.send("RESET"); rec.wait_for(lambda l: l.strip() == "READY", 3)
    cap.note("STEP operator: PRESS the RST button on the hub now")
    print(">>> PRESS the RST button on the hub now (waiting up to 120 s)")
    if not cap.wait_for(lambda l: "MSG msg_type=HUB_BOOT" in l, 120):
        return finish_run(cap, rec, path, a, sha0, "BLOCKED", "no HUB_BOOT within 120 s: the hub was not reset")
    time.sleep(3)
    bench.get_dump(cap)
    sleep_note(cap, 62.0, "observing the motor pin after boot")
    rec.send("STAT"); time.sleep(0.5)
    cap.note("PARAM case=tc29b observe_s=65")
    v, msg = analyze_tc29b(combined(cap, rec))
    return finish_run(cap, rec, path, a, sha0, v, msg)


def run_tc29c(a):
    hb, margin = 360, 60
    if a.rehearsal:
        hb = a.heartbeat or hb
        margin = a.margin if a.margin is not None else margin
    cap, rec, path, sha0, err = setup_run(a, ["case=tc29c node=2 heartbeat_s=%d margin_s=%d" % (hb, margin)])
    if err:
        return bench.finish(cap, path, a, sha0, "BLOCKED", err)
    d0 = bench.configure(cap, hb, margin)
    if not d0:
        return finish_run(cap, rec, path, a, sha0, "BLOCKED", "could not set heartbeat/margin or read the dump (setup failure)")
    timeout_s = 2 * hb + margin
    base = max(n["last_counter"] for n in d0["nodes"].values()) + 100
    ctr = {n: base + n for n in range(1, 6)}
    cap.note("EXPECT the motor starts <= 200 ms after node 2 goes OFFLINE (timeout %d s) and not before" % timeout_s)
    cap.send("marker on")
    if not cap.wait_for(lambda l: "PPE_HOOK marker on ok" in l, 3):
        return finish_run(cap, rec, path, a, sha0, "BLOCKED", "the hub did not accept `marker on`")
    err = quiet_marker(rec)
    if err:
        return finish_run(cap, rec, path, a, sha0, "BLOCKED", err)
    prime(cap, base)
    time.sleep(3.0)                              # every node WORN, the alert is off
    last2 = base + 2000
    send_pkt(cap, 2, last2, 1)                   # node 2's last packet: sent after the alert has ended
    time.sleep(1.0)
    cap.note("PARAM case=tc29c node=2 ctr=%d timeout_s=%d" % (last2, timeout_s))
    cap.note("STEP node 2 is silent from now; nodes 1, 3, 4, 5 are kept alive through the hub hook")
    t0 = time.monotonic()
    nxt = t0 + hb
    n_alive = 100
    off = None
    start_idx = bench.mark(cap)
    while time.monotonic() < t0 + timeout_s + 90 and off is None:
        if time.monotonic() >= nxt:
            for n in (1, 3, 4, 5):
                n_alive += 1
                send_pkt(cap, n, base + 1000 + n_alive, 1)
                time.sleep(0.8)
            nxt += hb
        off, _ = bench.wait_line(cap, r"MSG msg_type=NODE_OFFLINE .* node_id=2 ", start_idx, 0.5)
        print("  t+%.0f s" % (time.monotonic() - t0), end="\r")
    time.sleep(3)
    send_pkt(cap, 2, base + 5000, 1)           # node 2 comes back: the alert stops (cleanup, also shows the stop)
    time.sleep(2)
    rec.send("STAT"); time.sleep(0.5)
    v, msg = analyze_tc29c(combined(cap, rec), 2, last2, timeout_s)
    return finish_run(cap, rec, path, a, sha0, v, msg)


def analyze_any(text):
    """Result recording re-checks the log with the analyzer that belongs to it (motor cases here, node cases in s5_bench)."""
    case = bench.params(text).get("case", "")
    return analyze_text(text) if case == "cal" or case.startswith(("tc29", "tc30")) else bench.analyze_text(text)


def cmd_result(a):
    orig = bench.analyze_text
    bench.analyze_text = analyze_any
    try:
        return bench.cmd_result(a)
    finally:
        bench.analyze_text = orig


def arm_alert(cap):
    """Every node WORN, then node 2 NOT_WORN: the alert pattern runs. Counters come from a fresh dump."""
    d = bench.get_dump(cap)
    base = max(n["last_counter"] for n in d["nodes"].values()) + 100 if d else 100000
    prime(cap, base)
    time.sleep(1.5)
    send_pkt(cap, 2, base + 20, 0)
    time.sleep(2.0)
    return base


def clear_alert(cap, base):
    send_pkt(cap, 2, base + 21, 1)
    time.sleep(1.5)


def verdict_cleanup(cap, rec, path, a, sha0, v, msg, cleanup=True):
    if cleanup:
        cap.note("STEP cleanup: fault wdt returns the hub to a normal start")
        m = bench.mark(cap)
        cap.send("fault wdt")
        bench.wait_line(cap, r"MSG msg_type=HUB_BOOT", m, 40)
        time.sleep(1.0)
    return finish_run(cap, rec, path, a, sha0, v, msg)


def run_tc30a(a):
    cap, rec, path, sha0, err = setup_run(a, ["case=tc30a"])
    if err:
        return bench.finish(cap, path, a, sha0, "BLOCKED", err)
    cap.note("EXPECT HUB_FAULT NVS_WRITE and GUARD with the alert running; no ON above 400 ms; the guard cuts one ON run at about 395 ms; the pattern resumes")
    cap.note("PARAM case=tc30a")
    rec.send("RESET"); rec.wait_for(lambda l: l.strip() == "READY", 3)
    base = arm_alert(cap)
    sleep_note(cap, 4.0, "alert pattern running")
    cap.send("fault nvs-write")
    sleep_note(cap, 4.0, "after fault nvs-write")
    cap.send("fault guard")
    sleep_note(cap, 8.0, "after fault guard")
    clear_alert(cap, base)
    rec.send("STAT"); time.sleep(0.5)
    time.sleep(0.5)
    v, msg = analyze_tc30a(bench.cap_text(path))
    return finish_run(cap, rec, path, a, sha0, v, msg)


def run_tc30b(a):
    kinds = [("wdt", "fault wdt"), ("nvs-read", "fault nvs-read"), ("stall", "stall 3")]
    cap, rec, path, sha0, err = setup_run(a, ["case=tc30b"])
    if err:
        return bench.finish(cap, path, a, sha0, "BLOCKED", err)
    cap.note("EXPECT after each reboot fault: HUB_BOOT then the right HUB_FAULT code; no ON above 400 ms; the motor quiet until the alert is armed again")
    cap.note("PARAM case=tc30b faults=%s" % ",".join(k for k, _ in kinds))
    rec.send("RESET"); rec.wait_for(lambda l: l.strip() == "READY", 3)
    for i, (kind, cmd) in enumerate(kinds):
        arm_alert(cap)
        sleep_note(cap, 3.0, "alert pattern running before the %s fault" % kind)
        m = bench.mark(cap)
        cap.send(cmd)
        line, pos = bench.wait_line(cap, r"MSG msg_type=HUB_BOOT", m, 100)
        if line is None:
            return finish_run(cap, rec, path, a, sha0, "FAIL", "no HUB_BOOT within 100 s after the %s fault" % kind)
        bench.wait_line(cap, r"MSG msg_type=HUB_FAULT", pos, 6)
        sleep_note(cap, 4.0, "motor must stay quiet after the %s reboot" % kind)
        cap.note("STEP rearm %d" % (i + 1))
    time.sleep(0.5)
    v, msg = analyze_tc30b(bench.cap_text(path))
    return finish_run(cap, rec, path, a, sha0, v, msg)


def after_fault_boot(a, cap, rec, path, sha0, cmd, hb, margin):
    m = bench.mark(cap)
    cap.send(cmd)
    line, pos = bench.wait_line(cap, r"MSG msg_type=HUB_BOOT", m, 40)
    if line is None:
        return None, "no HUB_BOOT within 40 s after %s" % cmd
    time.sleep(3.0)
    if a.rehearsal and (hb or margin is not None):
        if not bench.configure(cap, hb or 360, margin if margin is not None else 60):
            return None, "rehearsal: could not set heartbeat/margin"
    return pos, None


def run_tc30c(a):
    hb, margin = 360, 60
    if a.rehearsal:
        hb = a.heartbeat or hb
        margin = a.margin if a.margin is not None else margin
    timeout_s = 2 * hb + margin
    cap, rec, path, sha0, err = setup_run(a, ["case=tc30c timeout_s=%d" % timeout_s])
    if err:
        return bench.finish(cap, path, a, sha0, "BLOCKED", err)
    cap.note("EXPECT NO_KEY within 2 s of boot, KEY_FAULT pattern (ON 100, OFF 100 x3, low 1800 after the third ON), never the alert pattern, also after the nodes went OFFLINE")
    cap.note("PARAM case=tc30c timeout_s=%d" % timeout_s)
    rec.send("RESET"); rec.wait_for(lambda l: l.strip() == "READY", 3)
    pos, err = after_fault_boot(a, cap, rec, path, sha0, "fault key", hb if a.rehearsal else 0, margin if a.rehearsal else None)
    if err:
        return finish_run(cap, rec, path, a, sha0, "BLOCKED", err)
    t0 = time.monotonic()
    off = None
    while time.monotonic() < t0 + timeout_s + 60 and off is None:
        off, _ = bench.wait_line(cap, r"MSG msg_type=NODE_OFFLINE", pos, 1.0)
        print("  after boot +%.0f s" % (time.monotonic() - t0), end="\r")
    sleep_note(cap, 10.0, "pulses after the nodes went OFFLINE")
    cap.send("dump"); time.sleep(1.5)
    cap.send("motor"); time.sleep(1.0)
    time.sleep(0.5)
    v, msg = analyze_tc30c(bench.cap_text(path))
    return verdict_cleanup(cap, rec, path, a, sha0, v, msg)


def run_tc30d(a):
    hb, margin = 360, 60
    if a.rehearsal:
        hb = a.heartbeat or hb
        margin = a.margin if a.margin is not None else margin
    timeout_s = 2 * hb + margin
    cap, rec, path, sha0, err = setup_run(a, ["case=tc30d timeout_s=%d" % timeout_s])
    if err:
        return bench.finish(cap, path, a, sha0, "BLOCKED", err)
    cap.note("EXPECT REGISTRY reported, accept disabled, all nodes OFFLINE %d..%d s after boot, the alert pattern runs" % (timeout_s, timeout_s + 5))
    cap.note("PARAM case=tc30d timeout_s=%d" % timeout_s)
    rec.send("RESET"); rec.wait_for(lambda l: l.strip() == "READY", 3)
    pos, err = after_fault_boot(a, cap, rec, path, sha0, "fault registry", hb if a.rehearsal else 0, margin if a.rehearsal else None)
    if err:
        return finish_run(cap, rec, path, a, sha0, "BLOCKED", err)
    cap.send("dump"); time.sleep(1.5)
    send_pkt(cap, 3, 5, 1)                          # a valid packet: it must not be accepted
    time.sleep(1.5)
    t0 = time.monotonic()
    off = None
    while time.monotonic() < t0 + timeout_s + 60 and off is None:
        off, _ = bench.wait_line(cap, r"MSG msg_type=NODE_OFFLINE .* node_id=5 ", pos, 1.0)
        print("  after boot +%.0f s" % (time.monotonic() - t0), end="\r")
    sleep_note(cap, 9.0, "alert pattern after OFFLINE")
    cap.send("motor"); time.sleep(1.0)
    time.sleep(0.5)
    v, msg = analyze_tc30d(bench.cap_text(path))
    return verdict_cleanup(cap, rec, path, a, sha0, v, msg)


def cmd_analyze(a):
    v, msg = analyze_text(bench.cap_text(a.log))
    print("%s: %s" % (v, msg))
    return {"PASS": 0, "FAIL": 1}.get(v, 3)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("cal")
    p.add_argument("--hub", required=True)
    p.add_argument("--rec", required=True)
    p.add_argument("--rehearsal", action="store_true")
    p.set_defaults(fn=run_cal)
    for name, fn in (("tc29a", run_tc29a), ("tc29b", run_tc29b), ("tc29c", run_tc29c), ("tc30a", run_tc30a), ("tc30b", run_tc30b),
                     ("tc30c", run_tc30c), ("tc30d", run_tc30d)):
        p = sub.add_parser(name)
        p.add_argument("--tc", required=True)
        p.add_argument("--hub", required=True)
        p.add_argument("--rec", required=True)
        p.add_argument("--rehearsal", action="store_true")
        p.add_argument("--heartbeat", type=int)
        p.add_argument("--margin", type=int)
        p.set_defaults(fn=fn)
    p = sub.add_parser("result")
    p.add_argument("--tc", required=True)
    p.add_argument("--result", required=True, choices=["PASS", "FAIL", "BLOCKED"])
    p.add_argument("--note")
    p.set_defaults(fn=cmd_result)
    p = sub.add_parser("analyze")
    p.add_argument("--log", required=True)
    p.set_defaults(fn=cmd_analyze)
    a = ap.parse_args(argv)
    return a.fn(a)


if __name__ == "__main__":
    sys.exit(main())
