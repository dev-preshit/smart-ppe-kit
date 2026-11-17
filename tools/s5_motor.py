#!/usr/bin/env python3
"""S5 motor pin edge recording and analysis (TC-29, TC-30, TC-74 support). TEST TOOL.

Alternative equipment (decision D8): no logic analyzer. A spare ESP32-C3 running tools/edge_recorder records the edges
of the hub motor pin (channel 0, GPIO 4 <- hub GPIO 5 before the driver) and of the hub marker pin (channel 1, GPIO 3 <- hub
GPIO 6) with its own 1 MHz clock. The recorder is calibrated first against a known pulse train that the hub generates
on the marker pin (hub hook `pulse`); the measured error is reported in the log.

  python tools/s5_motor.py cal --hub COM3 --rec COM8 --rehearsal      # 21 s, scratch log
  python tools/s5_motor.py cal --hub COM3 --rec COM8                  # evidence log test_logs/S5/CAL-recorder_<sha7>.log
  python tools/s5_motor.py analyze --log <log>                        # re-run the analyzer, no hardware

Wiring for the calibration: recorder GPIO 3 <- hub GPIO 6, recorder GND <- hub GND (nothing else on GPIO 6).
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


def edges(text):
    out = []
    for idx, line in enumerate(text.splitlines()):
        m = RE_EDGE.match(line.rstrip("\r"))
        if m:
            out.append(dict(idx=idx, t=int(m.group(1)), ch=int(m.group(2)), lvl=int(m.group(3)), pc=bench.pc_seconds(line)))
    return out


def wrap(later, earlier):
    return (later - earlier) & 0xFFFFFFFF


def pairs(ev):
    """Consecutive edges of one channel as (level of the run, duration in us, missed_edge flag)."""
    return [(a["lvl"], wrap(b["t"], a["t"]), a["lvl"] == b["lvl"]) for a, b in zip(ev, ev[1:])]


def analyze_cal(text, on_ms, off_ms, count, ch=1):
    ev = [e for e in edges(text) if e["ch"] == ch]
    if not ev:
        return "BLOCKED", "no edges from the recorder on channel %d: wiring or recorder problem (setup failure)" % ch
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


def analyze_text(text):
    p = bench.params(text)
    if p.get("case") == "cal":
        return analyze_cal(text, int(p["on_ms"]), int(p["off_ms"]), int(p["count"]))
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
    p = sub.add_parser("analyze")
    p.add_argument("--log", required=True)
    p.set_defaults(fn=cmd_analyze)
    a = ap.parse_args(argv)
    return a.fn(a)


if __name__ == "__main__":
    sys.exit(main())
