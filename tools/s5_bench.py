#!/usr/bin/env python3
"""S5 bench driver and analyzer: TC-27 (offline detection) and TC-31 (hub boot). TEST TOOL, public Appendix A key.

The hub is the unit under test (S3 hub on COM3, bench build). The spare ESP32-C3 running tools/injector acts as the
node (A-33, owner decision D4): "power off" = the injector stops, "drop one heartbeat" = the injector skips one tick.
All timing is read from the HUB's own t_ms stamps (ACC line of the last accepted packet -> NODE_OFFLINE message);
PC time only orders lines and is a cross-check. Alternative equipment (no EQ-05 power switch, no skiptx): written in
every log header. The hub is reset with its RST button (A-37), never through DTR/RTS: ports are opened with both low.

  python tools/s5_bench.py offline --tc TC-27a --case default --hub COM3 --inj COM8     # about 13 min
  python tools/s5_bench.py offline --tc TC-27b --case margin0 --hub COM3 --inj COM8     # about 12 min
  python tools/s5_bench.py offline --tc TC-27c --case drop1   --hub COM3 --inj COM8     # about 14 min
  python tools/s5_bench.py boot    --tc TC-31  --hub COM3 --inj COM8                    # about 14 min, press RST when asked
  python tools/s5_bench.py offline --tc TC-27a --case default --rehearsal --heartbeat 60 --margin 0 --hub COM3 --inj COM8
                                                                                         # 2 min rehearsal, scratch log
  python tools/s5_bench.py analyze --log test_logs/S5/TC-27a_<sha7>.log                  # re-run the analyzer, no hardware
  python tools/s5_bench.py result  --tc TC-27a --result PASS --note "780.9 s"             # only after a PASS verdict

Evidence logs: test_logs/S5/<TC>_<sha7>.log (one run per file, the driver refuses to append) and one row per TC in
test_logs/summary_s5.csv through tools/logger.py. A rehearsal writes to test_logs/scratch/ and can never be recorded.
Never run this and another program on the same COM port.
"""
import argparse
import datetime
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import logger  # noqa: E402

SPRINT = "S5"
SLACK_MS = 5000                      # TC-27 / TC-31 window: timeout .. timeout + 5 s (FR-27, V12-04)
PC_TOLERANCE_MS = 2000               # PC time stamps and hub t_ms must agree on the measured interval within this
EQUIPMENT = ("alternative equipment: no EQ-05 power switch and no skiptx; the spare ESP32-C3 running tools/injector "
             "acts as the node; the hub is reset with its RST button (decisions A-33, A-37)")

PFX = r"^(?:\S+ \| )?"
RE_PC = re.compile(r"^(\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d(?:\.\d+)?) \| ")
RE_ACC = re.compile(PFX + r"ACC t_ms=(\d+) t_us=\d+ node=(\d) ctr=(\d+) state=(\d) batt=\d+ flags=0x[0-9A-Fa-f]+ missed=\d+ prev=(\w+) now=(\w+)")
RE_OFF = re.compile(PFX + r"MSG msg_type=NODE_OFFLINE vest_id=\d+ node_id=(\d) state=OFFLINE last_seen_age_s=(\d+) t_ms=(\d+)")
RE_BOOT = re.compile(PFX + r"MSG msg_type=HUB_BOOT vest_id=\d+ reset_reason=(\d+) fw=(\S+) t_ms=(\d+)")
RE_COMP = re.compile(PFX + r"MSG msg_type=COMPLIANCE vest_id=\d+ state=(\w+) nodes_not_ok=\[([\d,]*)\] t_ms=(\d+)")
RE_DBEGIN = re.compile(PFX + r"DUMP begin t_ms=(\d+)")
RE_DHUB = re.compile(PFX + r"DUMP hub vest=\S+ accept_enabled=(\d) registry_ok=(\d) vest_state=(\w+) alert_active=(\d) "
                     r"heartbeat_s=(\d+) margin_s=(\d+) offline_timeout_s=(\d+)")
RE_DNODE = re.compile(PFX + r"DUMP node=(\d) state=(\w+) last_counter=(\d+) last_seen_ms=(\d+)")
RE_DEND = re.compile(PFX + r"DUMP end")
RE_PARAM = re.compile(PFX + r"PARAM (.*)$")
RE_VERDICT = re.compile(PFX + r"VERDICT (PASS|FAIL|BLOCKED)\b(.*)$")
RE_SETOK = re.compile(r"PPE_HOOK set (margin|heartbeat) (\d+) ok heartbeat_s=(\d+) margin_s=(\d+) offline_timeout_s=(\d+)")


# ------------------------------------------------------------------------------------------------ log parsing
def events(text):
    """Ordered list of dicts (kind, idx, ...). Works on logs with the 'PC time | ' prefix and on raw device lines."""
    ev, cur = [], None
    for idx, line in enumerate(text.splitlines()):
        line = line.rstrip("\r")
        pc = pc_seconds(line)
        m = RE_ACC.match(line)
        if m:
            ev.append(dict(kind="acc", idx=idx, pc=pc, t=int(m.group(1)), node=int(m.group(2)), ctr=int(m.group(3)),
                           state=int(m.group(4)), prev=m.group(5), now=m.group(6)))
            continue
        m = RE_OFF.match(line)
        if m:
            ev.append(dict(kind="off", idx=idx, pc=pc, node=int(m.group(1)), age_s=int(m.group(2)), t=int(m.group(3))))
            continue
        m = RE_BOOT.match(line)
        if m:
            ev.append(dict(kind="boot", idx=idx, pc=pc, reason=int(m.group(1)), fw=m.group(2), t=int(m.group(3))))
            continue
        m = RE_COMP.match(line)
        if m:
            ev.append(dict(kind="comp", idx=idx, state=m.group(1), t=int(m.group(3))))
            continue
        m = RE_DBEGIN.match(line)
        if m:
            cur = dict(kind="dump", idx=idx, t=int(m.group(1)), hub={}, nodes={})
            continue
        if cur is not None:
            m = RE_DHUB.match(line)
            if m:
                cur["hub"] = dict(vest_state=m.group(3), alert=int(m.group(4)), heartbeat_s=int(m.group(5)),
                                  margin_s=int(m.group(6)), timeout_s=int(m.group(7)))
                continue
            m = RE_DNODE.match(line)
            if m:
                cur["nodes"][int(m.group(1))] = dict(state=m.group(2), last_counter=int(m.group(3)), last_seen=int(m.group(4)))
                continue
            if RE_DEND.match(line):
                ev.append(cur)
                cur = None
    return ev


def pc_seconds(line):
    """PC time stamp of a log line in seconds, or None for a raw device line."""
    m = RE_PC.match(line)
    if not m:
        return None
    try:
        return datetime.datetime.fromisoformat(m.group(1)).timestamp()
    except ValueError:
        return None


def pc_check(d_hub_ms, e_from, e_to):
    """None when the PC clock agrees with the hub clock on this interval (or the lines carry no PC time), else a message."""
    if e_from.get("pc") is None or e_to.get("pc") is None:
        return None
    d_pc = (e_to["pc"] - e_from["pc"]) * 1000.0
    if abs(d_pc - d_hub_ms) > PC_TOLERANCE_MS:
        return "PC time says %.3f s but the hub clock says %.3f s (more than %.0f s apart)" % (d_pc / 1000.0, d_hub_ms / 1000.0, PC_TOLERANCE_MS / 1000.0)
    return None


def params(text):
    """PARAM lines written by the driver: 'case=offline node=2 ctr=1100 ...' -> dict of strings (later lines win)."""
    p = {}
    for line in text.splitlines():
        m = RE_PARAM.match(line.rstrip("\r"))
        if m:
            p.update(dict(kv.split("=", 1) for kv in m.group(1).split() if "=" in kv))
    return p


def diff_ms(later, earlier):
    return (later - earlier) & 0xFFFFFFFF          # hub t_ms is 32 bit


# ------------------------------------------------------------------------------------------------ analyzers
def analyze_offline(text, node, ctr, timeout_s):
    """TC-27 steps 1 and 2: OFFLINE between timeout and timeout + 5 s after the last accepted packet (D4: the hub
    last_seen = the ACC of the first accepted copy). Fails when no NODE_OFFLINE comes."""
    ev = events(text)
    acc = [e for e in ev if e["kind"] == "acc" and e["node"] == node and e["ctr"] == ctr]
    if not acc:
        return "BLOCKED", "no ACC line for node %d ctr %d: the packet was not accepted (setup failure, not a result)" % (node, ctr)
    a = acc[0]
    if [e for e in ev if e["kind"] == "acc" and e["node"] == node and e["idx"] > a["idx"]]:
        return "BLOCKED", "node %d was accepted again after ctr %d: it was not silent" % (node, ctr)
    off = [e for e in ev if e["kind"] == "off" and e["node"] == node and e["idx"] > a["idx"]]
    if not off:
        return "FAIL", "no NODE_OFFLINE for node %d after the last packet" % node
    d = diff_ms(off[0]["t"], a["t"])
    lo, hi = timeout_s * 1000, timeout_s * 1000 + SLACK_MS
    msg = "OFFLINE %.3f s after the last accepted packet (window %d to %d s), hub t_ms %d -> %d, last_seen_age_s=%d" % (
        d / 1000.0, timeout_s, timeout_s + SLACK_MS // 1000, a["t"], off[0]["t"], off[0]["age_s"])
    if abs(off[0]["age_s"] * 1000 - d) > 1000:
        return "FAIL", msg + "; last_seen_age_s disagrees with the t_ms difference"
    bad = pc_check(d, a, off[0])
    if bad:
        return "FAIL", msg + "; " + bad
    return ("PASS" if lo <= d <= hi else "FAIL"), msg


def analyze_drop1(text, node, ctr1, ctr2, heartbeat_s, timeout_s):
    """TC-27 step 3: one heartbeat (all 5 copies) skipped, the next one arrives 2 x HEARTBEAT_S after the first and the
    node never went OFFLINE in between and after."""
    ev = events(text)
    a1 = [e for e in ev if e["kind"] == "acc" and e["node"] == node and e["ctr"] == ctr1]
    a2 = [e for e in ev if e["kind"] == "acc" and e["node"] == node and e["ctr"] == ctr2]
    if not a1 or not a2:
        return "BLOCKED", "missing ACC for ctr %d or ctr %d (a packet was not accepted)" % (ctr1, ctr2)
    gap = diff_ms(a2[0]["t"], a1[0]["t"])
    if gap <= timeout_s * 1000 and abs(gap - 2 * heartbeat_s * 1000) > 5000:
        return "BLOCKED", "the gap between the two packets was %.1f s, not about %d s: driver timing problem" % (gap / 1000.0, 2 * heartbeat_s)
    if gap >= timeout_s * 1000:
        return "BLOCKED", "gap %.1f s is not below the timeout %d s: the test would be meaningless" % (gap / 1000.0, timeout_s)
    off = [e for e in ev if e["kind"] == "off" and e["node"] == node and e["idx"] > a1[0]["idx"]]
    msg = "gap between accepted packets %.1f s (one heartbeat skipped), timeout %d s, second packet prev=%s" % (
        gap / 1000.0, timeout_s, a2[0]["prev"])
    if off:
        return "FAIL", msg + "; NODE_OFFLINE for node %d at hub t_ms %d" % (node, off[0]["t"])
    bad = pc_check(gap, a1[0], a2[0])
    if bad:
        return "FAIL", msg + "; " + bad
    if a2[0]["prev"] != "WORN":
        return "FAIL", msg + "; the node was not WORN when the second packet arrived"
    return "PASS", msg + "; no NODE_OFFLINE for the node"


def analyze_boot(text, silent=5):
    """TC-31: reset a primed hub; states UNKNOWN, no alert, HUB_BOOT first; the silent node is OFFLINE
    780 to 785 s after boot (reference: its last_seen from the first dump after boot = the boot time)."""
    ev = events(text)
    boots = [e for e in ev if e["kind"] == "boot"]
    if not boots:
        return "BLOCKED", "no HUB_BOOT line: the hub was not reset (RST button)"
    if len(boots) > 1:
        return "BLOCKED", "%d HUB_BOOT lines in one run: the hub rebooted again" % len(boots)
    b = boots[0]
    before = [e for e in ev if e["kind"] == "dump" and e["idx"] < b["idx"]]
    if not before or all(n["state"] == "UNKNOWN" for n in before[-1]["nodes"].values()):
        return "BLOCKED", "no dump before the reset with a known node state: the hub was not primed, UNKNOWN after boot would prove nothing"
    after = [e for e in ev if e["kind"] == "dump" and e["idx"] > b["idx"]]
    if not after:
        return "BLOCKED", "no dump after the boot"
    d1 = after[0]
    if diff_ms(d1["t"], b["t"]) > 30000:
        return "BLOCKED", "first dump after boot was %.1f s after HUB_BOOT: too late to show the boot state" % (diff_ms(d1["t"], b["t"]) / 1000.0)
    bad = [i for i, n in sorted(d1["nodes"].items()) if n["state"] != "UNKNOWN"]
    if len(d1["nodes"]) != 5 or bad:
        return "FAIL", "states after reset are not all UNKNOWN (nodes %s)" % (bad or "missing")
    if d1["hub"].get("alert") != 0 or d1["hub"].get("vest_state") != "NON_COMPLIANT":
        return "FAIL", "after reset alert_active=%s vest_state=%s (want 0 and NON_COMPLIANT)" % (d1["hub"].get("alert"), d1["hub"].get("vest_state"))
    lines = text.splitlines()
    starts = [i for i, l in enumerate(lines) if i < b["idx"] and re.search(r"BOOT reset_reason=|HUB boot \(", l)]
    if not starts:
        return "BLOCKED", "no boot print before HUB_BOOT: the log does not show the start of the boot"
    if [e for e in ev if e["kind"] in ("comp", "off", "acc") and starts[-1] < e["idx"] < b["idx"]]:
        return "FAIL", "a message or packet came between the start of the boot and HUB_BOOT: HUB_BOOT was not first"
    if [e for e in ev if e["kind"] == "comp" and e["idx"] > b["idx"]]:
        return "FAIL", "a COMPLIANCE message appeared after boot (no transition is allowed while node %d is silent)" % silent
    ref = d1["nodes"][silent]["last_seen"]
    timeout_s = d1["hub"]["timeout_s"]
    off = [e for e in ev if e["kind"] == "off" and e["node"] == silent and e["idx"] > b["idx"]]
    if not off:
        return "FAIL", "no NODE_OFFLINE for the silent node %d" % silent
    d = diff_ms(off[0]["t"], ref)
    others = [e for e in ev if e["kind"] == "off" and e["node"] != silent and e["idx"] > b["idx"]]
    if others:
        return "FAIL", "node %d also went OFFLINE although its heartbeats were sent" % others[0]["node"]
    last = [e for e in ev if e["kind"] == "dump" and e["idx"] > off[0]["idx"]]
    if not last or last[-1]["hub"].get("alert") != 1 or last[-1]["nodes"].get(silent, {}).get("state") != "OFFLINE":
        return "FAIL", "no dump after the OFFLINE showing node %d OFFLINE and alert_active=1" % silent
    bad = pc_check(diff_ms(off[0]["t"], b["t"]), b, off[0])
    if bad:
        return "FAIL", "silent node %d: %s" % (silent, bad)
    lo, hi = timeout_s * 1000, timeout_s * 1000 + SLACK_MS
    msg = ("boot ok (HUB_BOOT first, all UNKNOWN, alert 0, NON_COMPLIANT, no COMPLIANCE); silent node %d OFFLINE %.3f s after boot "
           "(window %d to %d s), alert_active=1 afterwards" % (silent, d / 1000.0, timeout_s, timeout_s + SLACK_MS // 1000))
    return ("PASS" if lo <= d <= hi else "FAIL"), msg


def analyze_text(text):
    p = params(text)
    case = p.get("case")
    if case == "offline":
        return analyze_offline(text, int(p["node"]), int(p["ctr"]), int(p["timeout_s"]))
    if case == "drop1":
        return analyze_drop1(text, int(p["node"]), int(p["ctr1"]), int(p["ctr2"]), int(p["heartbeat_s"]), int(p["timeout_s"]))
    if case == "boot":
        return analyze_boot(text, int(p.get("silent", 5)))
    return "BLOCKED", "no PARAM line in the log: not a run of this driver"


# ------------------------------------------------------------------------------------------------ run helpers
def head_sha():
    return logger.git_sha7()


def tree_dirty():
    r = subprocess.run(["git", "status", "--porcelain", "--untracked-files=no"], cwd=logger.REPO, capture_output=True, text=True)
    return bool(r.stdout.strip())


def die(msg):
    print("ERROR:", msg)
    sys.exit(2)


def open_run(a, extra):
    sprint = "scratch" if a.rehearsal else SPRINT
    path = logger.log_path(a.tc, sprint=sprint)
    if os.path.exists(path):
        die("%s already exists: a log is one run, the driver never appends (move a failed setup to test_logs/invalid/)" % path)
    if not a.rehearsal and tree_dirty():
        die("tracked files are modified: commit tool changes before an evidence run (HEAD sha names the log)")
    header = [h for h in logger.header_lines(a.tc, False) if not h.startswith("tool=")] + ["tool=s5_bench.py (capture through tools/logger.py)", "mode=%s" % ("rehearsal" if a.rehearsal else "evidence"), EQUIPMENT] + extra
    cap = logger.Capture(a.hub, 115200, path, header)
    print("log:", path)
    time.sleep(0.5)
    return cap, path


def mark(cap):
    return len(cap.lines)


def wait_line(cap, regex, since, timeout_s):
    """First captured line after index `since` matching regex, else None."""
    rx = re.compile(regex)
    end = time.time() + timeout_s
    pos = since
    while time.time() < end:
        n = len(cap.lines)
        while pos < n:
            if rx.search(cap.lines[pos]):
                return cap.lines[pos], pos
            pos += 1
        time.sleep(0.05)
    return None, pos


def get_dump(cap):
    start = mark(cap)
    cap.send("dump")
    line, end = wait_line(cap, r"^DUMP end", start, 6)
    if line is None:
        return None
    ev = [e for e in events("\n".join(cap.lines[start:end + 1])) if e["kind"] == "dump"]
    return ev[-1] if ev else None


def set_hook(cap, what, value):
    start = mark(cap)
    cap.send("set %s %d" % (what, value))
    line, _ = wait_line(cap, r"PPE_HOOK set %s %d " % (what, value), start, 4)
    return bool(line and " ok " in line)


def configure(cap, hb, margin):
    if not set_hook(cap, "heartbeat", hb) or not set_hook(cap, "margin", margin):
        return None
    d = get_dump(cap)
    if not d or d["hub"].get("timeout_s") != 2 * hb + margin:
        return None
    return d


def idle_until(cap, deadline_mono, stop_regex, since, label):
    """Wait for a line (stop_regex) until the monotonic deadline; note progress once a minute."""
    rx = re.compile(stop_regex)
    pos, last_note = since, time.monotonic()
    while time.monotonic() < deadline_mono:
        n = len(cap.lines)
        while pos < n:
            if rx.search(cap.lines[pos]):
                return cap.lines[pos], pos
            pos += 1
        if time.monotonic() - last_note >= 60:
            last_note = time.monotonic()
            cap.note("STEP %s, %.0f s left" % (label, deadline_mono - time.monotonic()))
            print("  %s, %.0f s left" % (label, deadline_mono - time.monotonic()))
        time.sleep(0.2)
    return None, pos


def inj_send(cap, inj, node, ctr, state=1):
    import inject
    pkt = inject.signed(node, ctr, state)
    cap.note("INJ node=%d ctr=%d state=%d copies=5 spacing_ms=500 hex=%s" % (node, ctr, state, pkt.hex()))
    ok = inj.send(pkt)
    cap.note("INJ done ok=%s" % ok)
    return ok


def finish(cap, path, a, start_sha, verdict, msg):
    if not a.rehearsal and head_sha() != start_sha:
        verdict, msg = "BLOCKED", "HEAD changed during the run (%s -> %s): " % (start_sha, head_sha()) + msg
    cap.note("VERDICT %s %s" % (verdict, msg))
    cap.close()
    print("%s %s: %s" % (verdict, a.tc, msg))
    if a.rehearsal:
        print("(rehearsal: the result cannot be recorded)")
    return {"PASS": 0, "FAIL": 1}.get(verdict, 3)


# ------------------------------------------------------------------------------------------------ runs
def run_offline(a):
    import inject
    hb, margin = 360, (0 if a.case == "margin0" else 60)
    if a.rehearsal:
        hb = a.heartbeat or hb
        margin = a.margin if a.margin is not None else margin
    elif a.heartbeat is not None or a.margin is not None:
        die("--heartbeat/--margin are for --rehearsal only; evidence runs use --case")
    timeout_s = 2 * hb + margin
    cap, path = open_run(a, ["case=%s node=%d heartbeat_s=%d margin_s=%d timeout_s=%d" % (a.case, a.node, hb, margin, timeout_s)])
    sha0 = head_sha()
    cap.note("EXPECT %s" % ("OFFLINE %d to %d s after the last accepted packet" % (timeout_s, timeout_s + 5) if a.case != "drop1"
                            else "no NODE_OFFLINE although one heartbeat (all 5 copies) is skipped"))
    d0 = configure(cap, hb, margin)
    if not d0 or a.node not in d0["nodes"]:
        return finish(cap, path, a, sha0, "BLOCKED", "could not set heartbeat/margin or read the dump (setup failure, not a result)")
    inj = inject.InjectorLink(a.inj, 115200, copies=5, spacing_ms=500)
    ctr1 = d0["nodes"][a.node]["last_counter"] + 100
    if a.case == "drop1":
        cap.note("PARAM case=drop1 node=%d ctr1=%d ctr2=%d heartbeat_s=%d margin_s=%d timeout_s=%d" % (a.node, ctr1, ctr1 + 1, hb, margin, timeout_s))
        t0 = time.monotonic()
        m = mark(cap)
        inj_send(cap, inj, a.node, ctr1)
        if wait_line(cap, r"ACC .* node=%d ctr=%d " % (a.node, ctr1), m, 8)[0] is None:
            return finish(cap, path, a, sha0, "BLOCKED", "first packet not accepted (setup failure)")
        cap.note("STEP heartbeat at +%d s is skipped on purpose (all 5 copies)" % hb)
        line, _ = idle_until(cap, t0 + 2 * hb, r"MSG msg_type=NODE_OFFLINE .* node_id=%d " % a.node, m, "waiting for the second heartbeat")
        if line:
            return finish(cap, path, a, sha0, "FAIL", "NODE_OFFLINE for node %d before the second heartbeat although the gap is below the timeout" % a.node)
        m2 = mark(cap)
        inj_send(cap, inj, a.node, ctr1 + 1)
        idle_until(cap, time.monotonic() + 40, r"MSG msg_type=NODE_OFFLINE .* node_id=%d " % a.node, m2, "watching after the second heartbeat")
        cap.note("STEP end of watch")
        time.sleep(0.5)
        v, msg = analyze_drop1("\n".join(cap.lines), a.node, ctr1, ctr1 + 1, hb, timeout_s)
        return finish(cap, path, a, sha0, v, msg)
    cap.note("PARAM case=offline node=%d ctr=%d heartbeat_s=%d margin_s=%d timeout_s=%d" % (a.node, ctr1, hb, margin, timeout_s))
    m = mark(cap)
    t0 = time.monotonic()
    inj_send(cap, inj, a.node, ctr1)
    if wait_line(cap, r"ACC .* node=%d ctr=%d " % (a.node, ctr1), m, 8)[0] is None:
        return finish(cap, path, a, sha0, "BLOCKED", "packet not accepted (setup failure, not a result)")
    cap.note("STEP the injector stops now: node %d is silent" % a.node)
    idle_until(cap, t0 + timeout_s + 60, r"MSG msg_type=NODE_OFFLINE .* node_id=%d " % a.node, m, "waiting for OFFLINE")
    time.sleep(0.5)
    v, msg = analyze_offline("\n".join(cap.lines), a.node, ctr1, timeout_s)
    return finish(cap, path, a, sha0, v, msg)


def cap_text(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read()


def run_boot(a):
    import inject
    silent = 5
    cap, path = open_run(a, ["case=boot silent=%d" % silent])
    sha0 = head_sha()
    cap.note("EXPECT after RST: HUB_BOOT first, all nodes UNKNOWN, alert_active=0, NON_COMPLIANT, no COMPLIANCE; silent node %d OFFLINE 780 to 785 s after boot" % silent)
    cap.note("PARAM case=boot silent=%d" % silent)
    d0 = get_dump(cap)
    if not d0:
        return finish(cap, path, a, sha0, "BLOCKED", "no dump from the hub (setup failure)")
    base = max(n["last_counter"] for n in d0["nodes"].values()) + 100
    cap.note("STEP priming: every node WORN through the hub inject hook, so UNKNOWN after the reset means something")
    for n in range(1, 6):
        cap.send("inject " + inject.signed(n, base + n, 1).hex())
        time.sleep(0.8)
    d1 = get_dump(cap)
    if not d1 or any(v["state"] != "WORN" for v in d1["nodes"].values()):
        return finish(cap, path, a, sha0, "BLOCKED", "priming failed: not every node is WORN before the reset")
    cap.note("STEP operator: PRESS the RST button on the hub now")
    print(">>> PRESS the RST button on the hub now (waiting up to 120 s)")
    m = mark(cap)
    line, pos = wait_line(cap, r"MSG msg_type=HUB_BOOT", m, 120)
    if line is None:
        return finish(cap, path, a, sha0, "BLOCKED", "no HUB_BOOT within 120 s: the hub was not reset")
    boot_mono = time.monotonic()
    time.sleep(3)
    hb, margin = 360, 60
    if a.rehearsal and (a.heartbeat or a.margin is not None):
        hb = a.heartbeat or hb
        margin = a.margin if a.margin is not None else margin
        if not configure(cap, hb, margin):
            return finish(cap, path, a, sha0, "BLOCKED", "rehearsal: could not set heartbeat/margin")
    d2 = get_dump(cap)
    if not d2:
        return finish(cap, path, a, sha0, "BLOCKED", "no dump after the boot")
    timeout_s = d2["hub"]["timeout_s"]
    inj = inject.InjectorLink(a.inj, 115200, copies=5, spacing_ms=500)
    ctr = {n: base + 50 + n for n in range(1, 5)}
    deadline = boot_mono + timeout_s + 60
    next_batch = time.monotonic() + 2
    batch = 0
    off_line = None
    while time.monotonic() < deadline and off_line is None:
        if time.monotonic() >= next_batch:
            cap.note("STEP heartbeat batch %d for nodes 1-4 (node %d stays silent)" % (batch, silent))
            for n in range(1, 5):
                inj_send(cap, inj, n, ctr[n])
                ctr[n] += 1
            batch += 1
            next_batch += hb
        off_line, _ = wait_line(cap, r"MSG msg_type=NODE_OFFLINE .* node_id=%d " % silent, pos, 0.3)
        print("  boot+%.0f s" % (time.monotonic() - boot_mono), end="\r")
    time.sleep(2.5)
    get_dump(cap)
    time.sleep(0.5)
    v, msg = analyze_boot("\n".join(cap.lines), silent)
    return finish(cap, path, a, sha0, v, msg)


# ------------------------------------------------------------------------------------------------ analyze / result
def cmd_analyze(a):
    v, msg = analyze_text(cap_text(a.log))
    print("%s: %s" % (v, msg))
    return {"PASS": 0, "FAIL": 1}.get(v, 3)


def cmd_result(a, root=None):
    sha = head_sha()
    path = logger.log_path(a.tc, sprint=SPRINT, sha=sha, root=(os.path.join(root, "test_logs") if root else None))
    if not os.path.exists(path):
        die("no evidence log for %s at HEAD %s (%s)" % (a.tc, sha, path))
    text = cap_text(path)
    vs = [RE_VERDICT.match(l.rstrip("\r")) for l in text.splitlines()]
    vs = [m for m in vs if m]
    if not vs:
        die("the log has no VERDICT line: the run did not finish")
    if vs[-1].group(1) != a.result:
        die("the driver's VERDICT is %s, you asked to record %s: refused" % (vs[-1].group(1), a.result))
    if a.result == "PASS":
        v, _ = analyze_text(text)
        if v != "PASS":
            die("the analyzer no longer says PASS on this log (%s): refused" % v)
    with open(path, "a", encoding="utf-8", newline="\n") as f:
        f.write("# RESULT,%s,%s,%s\n" % (a.result, logger.now_iso(), a.note or ""))
    logger.append_summary(a.tc, a.result, sprint=SPRINT, sha=sha, root=(os.path.join(root, "test_logs") if root else None))
    print("recorded %s %s at %s" % (a.tc, a.result, sha))
    return 0


def build_parser():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    def hw(p):
        p.add_argument("--tc", required=True)
        p.add_argument("--hub", required=True)
        p.add_argument("--inj", required=True)
        p.add_argument("--rehearsal", action="store_true")
        p.add_argument("--heartbeat", type=int)
        p.add_argument("--margin", type=int)
    p = sub.add_parser("offline"); hw(p)
    p.add_argument("--case", choices=["default", "margin0", "drop1"], required=True)
    p.add_argument("--node", type=int, default=2)
    p.set_defaults(fn=run_offline)
    p = sub.add_parser("boot"); hw(p)
    p.set_defaults(fn=run_boot)
    p = sub.add_parser("analyze"); p.add_argument("--log", required=True); p.set_defaults(fn=cmd_analyze)
    p = sub.add_parser("result")
    p.add_argument("--tc", required=True)
    p.add_argument("--result", required=True, choices=["PASS", "FAIL", "BLOCKED"])
    p.add_argument("--note")
    p.set_defaults(fn=cmd_result)
    return ap


def main(argv=None):
    a = build_parser().parse_args(argv)
    return a.fn(a)


if __name__ == "__main__":
    sys.exit(main())
