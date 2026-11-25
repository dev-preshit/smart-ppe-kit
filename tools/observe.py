#!/usr/bin/env python3
"""observe.py - S4 observer tool (A-14): capture from tools/observer, burst analysis, injector calibration, result row.
All times come from the observer's own esp_timer clock (PKT and TIME lines); the PC time only orders lines (A-01).

  capture   --port COM6 --tc TC-10 --seconds 60 [--filter 21] [--waived] [--header TEXT]
            [--node COM9 --node-send "tx worn" --node-after 5] [--node-reset]   (the node's own lines are logged as NODE ..., A-20)
            [--injector COM4 --adv "<hex> <copies> <spacing_ms>" --adv-after 3]   (calibration: the injector sends known bursts)
  analyze   --tc TC-10 [--log PATH] [--copies 5] [--spacing 500] [--tol 50] [--quiet 10] [--expect-tx N] [--need-marks] [--node-id N]
            [--len 21] [--prefix 14ffffff] [--gap 0.2]       one 'copy' = one burst of PDUs (A-15); exit 0 = PASS, 1 = FAIL
  calibrate --tc OBS-CAL [--log PATH] --spacing 500        measured spacing error of the observer against the injector (no verdict)
  plan      --out plan.txt [--cycles 20 --hold 6 --grace 2 --hb 60 --heartbeats 20]   timed hook lines for a FOOT run (adc hook)
  result    --tc TC-10 --result PASS|FAIL|BLOCKED          one row in test_logs/summary_s4.csv

Log: test_logs/S4/<TC>_<sha7>.log. A log is never appended to by a second capture. Never run two programs on one COM port."""
import argparse, os, re, statistics, sys, threading, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import logger

SPRINT = "S4"
PKT = re.compile(r"\| PKT (\d+)\.(\d{6}) (\d+) (\d+) (-?\d+) ([0-9a-f]*)\s*$")
TIME = re.compile(r"\| TIME (\d+)\.(\d{6})\s*$")
LOSS = re.compile(r"\| (?:LOST (\d+)|STAT .*lost=(\d+))")
NODE_MARK = re.compile(r"\| NODE MARK COPY(\d) (\d+)")
FWLINE = re.compile(r"\| NODE FW common=([0-9a-f]{8}) node=([0-9a-f]{8}) dirty=(\d) type=(\d) hooks=(\d)")
NODE_TX = re.compile(r"\| NODE .*TX seq=(\d+) type=(event|heartbeat) ctr=(\d+) state=(\d+) boot=(\d) fault=(\d)")


def log_file(a):
    return a.log or logger.log_path(a.tc, SPRINT)


def parse(path):
    """-> (pdus [(us, len, evt, rssi, hex)], end_us, lost). end_us = last PKT or TIME time of the observer clock."""
    pk, end, lost = [], 0, 0
    with open(path, encoding="utf-8") as f:
        for ln in f:
            if "| OK reset" in ln:      # a capture starts with RESET: whatever came before it does not count
                pk, end, lost = [], 0, 0
                continue
            m = PKT.search(ln)
            if m:
                us = int(m.group(1)) * 1000000 + int(m.group(2))
                pk.append((us, int(m.group(3)), int(m.group(4)), int(m.group(5)), m.group(6))); end = max(end, us); continue
            m = TIME.search(ln)
            if m:
                end = max(end, int(m.group(1)) * 1000000 + int(m.group(2))); continue
            m = LOSS.search(ln)
            if m:
                lost = int(m.group(1) or m.group(2))
    return pk, end, lost


def parse_node(path):
    """Lines the node printed during the capture: its copy-start marks (own micros clock) and its TX lines."""
    seqs, cur, tx = [], None, []
    with open(path, encoding="utf-8") as f:
        for ln in f:
            if "NODETX FAILED" in ln:
                tx.append(("FAILED", ln.split("FAILED", 1)[1].strip()))
                continue
            m = NODE_MARK.search(ln)
            if m:
                k, us = int(m.group(1)), int(m.group(2))
                if k == 0:
                    cur = []; seqs.append(cur)
                if cur is not None:
                    cur.append((k, us))
                continue
            m = NODE_TX.search(ln)
            if m:
                tx.append((int(m.group(1)), m.group(2), int(m.group(3)), int(m.group(4)), int(m.group(5)), int(m.group(6))))
    return seqs, tx


def bursts(pdus, gap_us):
    out = []
    for p in sorted(pdus):
        if out and p[0] - out[-1][-1][0] <= gap_us:
            out[-1].append(p)
        else:
            out.append([p])
    return out


def transmissions(pdus, gap_us):
    """Identical bytes = one transmission (the copies share one counter). -> [(hex, [burst, ...])] by first time."""
    by = {}
    for p in pdus:
        by.setdefault(p[4], []).append(p)
    tx = [(h, bursts(v, gap_us)) for h, v in by.items()]
    tx.sort(key=lambda t: t[1][0][0][0])
    return tx


def decode(h):
    b = bytes.fromhex(h)
    if len(b) != 21:
        return "len=%d" % len(b)
    fl = b[4]
    return "node=%d vest=%d type=%s flags=0x%02x boot=%d low_batt=%d fault=%d state=%d batt=%d ctr=%d" % (
        b[6], b[5], "event" if fl & 0x20 else "heartbeat", fl, (fl >> 4) & 1, (fl >> 3) & 1, (fl >> 2) & 1,
        b[7], b[8], int.from_bytes(b[9:13], "little"))


def dec(h):
    b = bytes.fromhex(h)
    return {"event": bool(b[4] & 0x20), "boot": bool(b[4] & 0x10), "fault": bool(b[4] & 0x04), "state": b[7], "node": b[6],
            "ctr": int.from_bytes(b[9:13], "little")}


def spacings_ms(bs):
    return [(bs[i][0][0] - bs[i - 1][0][0]) / 1000.0 for i in range(1, len(bs))]


def emit(path, lines):
    with open(path, "a", encoding="utf-8", newline="\n") as f:
        for ln in lines:
            f.write("# ANALYSIS %s\n" % ln)
            print(ln)


def cmd_analyze(a):
    path = log_file(a)
    pk, end, lost = parse(path)
    pdus = [p for p in pk if p[1] == a.len and p[4].startswith(a.prefix)]
    tx = transmissions(pdus, int(a.gap * 1000000))
    errs, out = [], ["analyze %s: observer PDUs=%d, matching %d-byte PDUs=%d, transmissions=%d, lost=%d" % (
        os.path.basename(path), len(pk), a.len, len(pdus), len(tx), lost)]
    if not tx and a.expect_tx != 0:                  # --expect-tx 0 = TC-01 step 5: an unprovisioned board must send nothing
        errs.append("no matching transmission in the log")
    if lost:
        errs.append("observer queue overflow, lost=%d" % lost)
    wrong = [p for p in pdus if p[2] != 3]
    if wrong:
        errs.append("%d PDUs are not ADV_NONCONN_IND (evt 3), first evt=%d" % (len(wrong), wrong[0][2]))
    if a.expect_tx is not None and len(tx) != a.expect_tx:
        errs.append("expected %d transmissions, found %d" % (a.expect_tx, len(tx)))
    nid = getattr(a, "node_id", None)
    if nid is not None:                              # TC-01: the node_id decoded from the air must be the provisioned one
        for i, (h, _bs) in enumerate(tx, 1):
            if dec(h)["node"] != nid:
                errs.append("TX %d: node_id %d on the air, provisioned as %d" % (i, dec(h)["node"], nid))
    whole = open(path, encoding="utf-8").read()
    for bad in ("OBSERVER STALLED", "NODE RESET FAILED", "final TIME and STAT could not be sent", "FIRMWARE MISMATCH"):
        if bad in whole:
            errs.append("the capture reports: %s" % bad)
    if "# fw_check=required" in whole:               # process rule 2: this log must show the firmware of HEAD
        m = re.search(r"# fw_expected common=([0-9a-f]{8}) node=([0-9a-f]{8})", whole)
        prob = fw_problem(parse_fw(whole), (int(m.group(1), 16), int(m.group(2), 16), 0)) if m else "no fw_expected line in the header"
        if prob:
            errs.append("firmware identity: " + prob)
    nseqs, ntx = parse_node(path)
    for lost_cmd in [x for x in ntx if x[0] == "FAILED"]:      # a hook command that never reached the node makes the run meaningless
        errs.append("a node command was lost: %s" % lost_cmd[1])
    ntx = [x for x in ntx if x[0] != "FAILED"]
    need = getattr(a, "need_marks", False)
    judge_obs = not nseqs        # A-20: with node marks present the node decides the spacing; the observer error is about +/- 40 ms
    prev_start = None
    for i, (h, bs) in enumerate(tx, 1):
        sp = spacings_ms(bs)
        out.append("TX %d %s copies=%d pdus_per_burst=%s start_to_start_ms=%s" % (
            i, decode(h), len(bs), [len(b) for b in bs], ["%.1f" % s for s in sp]))
        if a.copies and len(bs) != a.copies:
            errs.append("TX %d: %d copies, expected %d" % (i, len(bs), a.copies))
        for k, s in enumerate(sp, 1):
            if judge_obs and abs(s - a.spacing) > a.tol:
                errs.append("TX %d: copy %d starts %.1f ms after copy %d, limit %d +/- %d" % (i, k + 1, s, k, a.spacing, a.tol))
        last_pdu = bs[-1][-1][0]
        quiet = (end - last_pdu) / 1000000.0
        if quiet < a.quiet:
            errs.append("TX %d: capture ends %.1f s after the last PDU, need %.1f s of observed silence" % (i, quiet, a.quiet))
        if prev_start is not None:
            out.append("TX %d first copy %.3f s after the first copy of TX %d" % (i, (bs[0][0][0] - prev_start) / 1e6, i - 1))
        prev_start = bs[0][0][0]
    if need and not nseqs:
        errs.append("no node copy marks in the log (A-20: the spacing is judged from the node's own marks)")
    for i, sq in enumerate(nseqs, 1):
        nsp = [((sq[j][1] - sq[j - 1][1]) & 0xFFFFFFFF) / 1000.0 for j in range(1, len(sq))]
        out.append("NODE seq %d marks=%d start_to_start_ms=%s (node clock, judged)" % (i, len(sq), ["%.1f" % s for s in nsp]))
        if [k for k, _ in sq] != list(range(len(sq))):
            errs.append("node seq %d: copy marks out of order or missing: %s" % (i, [k for k, _ in sq]))
        if a.copies and len(sq) != a.copies:
            errs.append("node seq %d: %d copy marks, expected %d" % (i, len(sq), a.copies))
        for k, s in enumerate(nsp, 1):
            if abs(s - a.spacing) > a.tol:
                errs.append("node seq %d: copy %d starts %.1f ms after copy %d, limit %d +/- %d" % (i, k + 1, s, k, a.spacing, a.tol))
    if nseqs and len(nseqs) != len(tx):
        errs.append("node sent %d sequences, the observer saw %d transmissions" % (len(nseqs), len(tx)))
    for i, (n, (h, _bs)) in enumerate(zip(ntx, tx), 1):        # what the node says it sent against what is on the air
        b = bytes.fromhex(h)
        _seq, typ, ctr, st, boot, flt = n
        want = (0x20 if typ == "event" else 0, 0x10 if boot else 0, 0x04 if flt else 0, 1 if st == 2 else 0, ctr)
        got = (b[4] & 0x20, b[4] & 0x10, b[4] & 0x04, b[7], int.from_bytes(b[9:13], "little"))
        if want != got:
            errs.append("TX %d: the node log says %s ctr=%d state=%d boot=%d fault=%d, the air has %s" % (i, typ, ctr, st, boot, flt, decode(h)))
    cyc, hbn = getattr(a, "cycles", 0), getattr(a, "heartbeats", 0)
    if cyc or hbn:                       # TC-07 / TC-10 run: boot event, N x (remove, wear) events, then heartbeats, counter +1 every time
        want = 1 + 2 * cyc + hbn
        if len(tx) != want:
            errs.append("expected %d transmissions (1 boot event + %d events + %d heartbeats), found %d" % (want, 2 * cyc, hbn, len(tx)))
        d = [dec(h) for h, _bs in tx]
        for i, x in enumerate(d):
            if i and x["node"] != d[0]["node"]:
                errs.append("TX %d: node %d, the first packet was node %d" % (i + 1, x["node"], d[0]["node"]))
            if i and x["ctr"] != d[i - 1]["ctr"] + 1:
                errs.append("TX %d: counter %d after %d, it must be +1" % (i + 1, x["ctr"], d[i - 1]["ctr"]))
            if i == 0:
                if not (x["event"] and x["boot"]):
                    errs.append("TX 1 must be the BOOT=1 event")
            elif i <= 2 * cyc:
                if not x["event"] or x["boot"]:
                    errs.append("TX %d must be an event with BOOT=0" % (i + 1))
                if x["state"] != 1 - d[i - 1]["state"]:
                    errs.append("TX %d: state %d after state %d, it must alternate" % (i + 1, x["state"], d[i - 1]["state"]))
            else:
                if x["event"] or x["boot"]:
                    errs.append("TX %d must be a heartbeat with BOOT=0" % (i + 1))
                if x["state"] != d[2 * cyc]["state"]:
                    errs.append("TX %d: heartbeat state %d, the last confirmed state was %d" % (i + 1, x["state"], d[2 * cyc]["state"]))
        if hbn and len(nseqs) == len(tx):
            hb_t = [nseqs[i][0][1] for i in range(2 * cyc + 1, len(nseqs))]
            gaps = [((hb_t[j] - hb_t[j - 1]) & 0xFFFFFFFF) / 1e6 for j in range(1, len(hb_t))]
            if gaps:
                out.append("HEARTBEAT first copy to first copy, node clock: min %.3f s mean %.3f s max %.3f s over %d gaps (reported, not judged here)" % (min(gaps), sum(gaps) / len(gaps), max(gaps), len(gaps)))
        out.append("RUN %d wear + %d remove events and %d heartbeats checked against the air" % (cyc, cyc, hbn))
    for e in errs:
        out.append("FINDING " + e)
    out.append("RESULT %s" % ("PASS" if not errs else "FAIL (%d)" % len(errs)))
    emit(path, out)
    return 1 if errs else 0


def cmd_calibrate(a):
    path = log_file(a)
    pk, end, lost = parse(path)
    pdus = [p for p in pk if p[1] == a.len and p[4].startswith(a.prefix)]
    tx = transmissions(pdus, int(a.gap * 1000000))
    sp = [s for _, bs in tx for s in spacings_ms(bs)]
    out = ["calibrate %s: transmissions=%d lost=%d nominal spacing %d ms" % (os.path.basename(path), len(tx), lost, a.spacing)]
    if not sp:
        out.append("FINDING no burst spacing in the log (injector did not send, or less than two bursts were seen)")
        emit(path, out)
        return 1
    err = [abs(s - a.spacing) for s in sp]
    per = [len(b) for _, bs in tx for b in bs]
    out.append("bursts=%d spacings=%d mean=%.2f ms min=%.1f max=%.1f stdev=%.2f" % (len(per), len(sp), statistics.mean(sp), min(sp), max(sp), statistics.pstdev(sp)))
    out.append("OBSERVER_ERROR_MS max=%.1f mean=%.2f (spacing seen minus nominal; the injector's own jitter is included)" % (max(err), statistics.mean(err)))
    out.append("pdus_per_burst min=%d max=%d mean=%.1f" % (min(per), max(per), statistics.mean(per)))
    if lost:
        out.append("FINDING observer queue overflow, lost=%d: these numbers are not valid" % lost)
    emit(path, out)
    return 1 if lost else 0


def cmd_result(a):
    logger.append_summary(a.tc, a.result, sprint=SPRINT)
    print("summary row written:", logger.summary_path(SPRINT), a.tc, a.result)
    return 0


def load_plan(path):
    """Lines of '<seconds from the start> <hook command>'. Never a key: a command longer than 40 characters is refused."""
    out = []
    with open(path, encoding="utf-8") as f:
        for ln in f:
            ln = ln.strip()
            if not ln or ln.startswith("#"):
                continue
            t, cmd = ln.split(None, 1)
            if cmd.startswith("say "):                          # a prompt for the person at the bench, never sent to the node
                out.append((float(t), cmd))
                continue
            if len(cmd) > 40 or "key" in cmd.lower().replace("testkey", ""):
                sys.exit("STOP: plan command not allowed (too long or key-like): %.12s..." % cmd)
            out.append((float(t), cmd))
    return out


def cmd_plan(a):
    """TC-07 / TC-10 run for a node that boots WORN (FOOT build with --adc-boot 2000, adc hook): N x (remove, wear), then heartbeats."""
    lines = ["# plan for observe.py capture --node-plan (seconds from the start of the capture, then the hook line)",
             "%g set grace %d" % (a.first - 2, a.grace)]
    t = a.first
    for _ in range(a.cycles):
        lines.append("%g adc 50" % t); lines.append("%g adc 2000" % (t + a.hold)); t += 2 * a.hold
    if a.hb:
        lines.append("%g set hb %d" % (t + 3, a.hb)); lines.append("%g tx hb" % (t + 4)); t += 4
    with open(a.out, "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    hb_end = t + (a.heartbeats - 1) * a.hb if a.hb else t
    print("plan written: %s, %d commands, last command at %g s; run at least --seconds %d (last heartbeat %g s + 2.1 s copies + 10 s silence + 15 s margin)"
          % (a.out, len(lines) - 1, t, int(hb_end + 2.1 + 10 + 15) + 1, hb_end))
    return 0


def last_sec(path):
    """The newest SEC number the observer wrote into the log (it prints one every second), or None."""
    try:
        with open(path, "rb") as f:
            f.seek(0, 2); n = f.tell(); f.seek(max(0, n - 4096))
            data = f.read().decode("utf-8", "replace")
    except OSError:
        return None
    m = list(re.finditer(r"\| SEC (\d+) ", data))
    return int(m[-1].group(1)) if m else None


def observer_watchdog(path, cap, stop, stalled, limit=8.0, step=1.0):
    """Stops a capture whose observer went quiet (USB drop, hub reset): such a run proves nothing."""
    last, t_change = None, time.time()
    while not stop.wait(step):
        cur = last_sec(path)
        if cur is not None and cur != last:
            last, t_change = cur, time.time()
        elif time.time() - t_change > limit:
            cap.note("OBSERVER STALLED: no SEC line for %d s, the capture was stopped and the run is invalid" % int(limit))
            stalled.set()
            return


def send_node(holder, cmd, cap, stalled, limit=20.0):
    """Writes a hook line to the node. A sleeping node's port comes and goes: retry every 0.3 s for up to `limit` seconds."""
    t_end = time.time() + limit
    tries = 0
    while True:
        tries += 1
        try:
            holder["ser"].write((cmd + "\n").encode())
            if tries > 1:
                cap.note("NODETX sent after %d tries: %s" % (tries, cmd))
            return True
        except Exception:
            if time.time() >= t_end or stalled.is_set():
                cap.note("NODETX FAILED (the node was asleep or its port closed): %s" % cmd)
                return False
            time.sleep(0.3)


PLAN_RESET_NOTE = "NODE RESET by RTS pulse (plan)"


def send_reset(holder, cap, stalled, limit=20.0):
    """A plan 'reset' line: the RTS reset pulse on the node's port, retried every 0.1 s while the port is closed (a node
    that is restarting has no port yet). Logged as PLAN_RESET_NOTE, or NODE RESET FAILED, which fails the analysis."""
    t_end = time.time() + limit
    while True:
        try:
            reset_pulse(holder["ser"])
            cap.note(PLAN_RESET_NOTE)
            return True
        except Exception:
            if time.time() >= t_end or stalled.is_set():
                cap.note("NODE RESET FAILED (plan reset, the node port stayed closed)")
                return False
            time.sleep(0.1)


def wait_port_stable(holder, limit, stable=1.5):
    """Waits until the node's port has been open for `stable` seconds in a row (a node that fell back to sleep is waited for again)."""
    t_w = time.time()
    since = None
    while time.time() - t_w < limit:
        if holder.get("ser") is not None:
            if since is None:
                since = time.time()
            if time.time() - since >= stable:
                return True
        else:
            since = None
        time.sleep(0.05)
    return False


def wait_until(t_abs, stalled):
    while True:
        left = t_abs - time.time()
        if left <= 0:
            return True
        if stalled.is_set():
            return False
        time.sleep(min(0.5, left))


def parse_fw(text):
    """(common tree hash, node tree hash, dirty) from the node's FW line, or None."""
    m = FWLINE.search(text)
    return None if not m else (int(m.group(1), 16), int(m.group(2), 16), int(m.group(3)))


def fw_problem(found, expected):
    """None when the node runs the firmware of HEAD (process rule 2), else the reason."""
    if found is None:
        return "the node sent no firmware identity line"
    if found[2]:
        return "the node's firmware was built from a working tree that differs from HEAD (dirty=1)"
    if (found[0], found[1]) != (expected[0], expected[1]):
        return "the node runs common=%08x node=%08x but HEAD is common=%08x node=%08x" % (found[0], found[1], expected[0], expected[1])
    return None


def reset_pulse(ser, hold=0.15):
    """Resets the C3 through its USB serial port: RTS high, then low, with DTR low. DTR is written again after every RTS
    change because the Windows usbser.sys driver sends the new line state to the device only when DTR is written (esptool
    does the same); without it the RTS pulse never reached the node (TRY-GPIO at b465b2a: no restart, no FW line)."""
    ser.dtr = False; ser.rts = True; ser.dtr = False
    time.sleep(hold)
    ser.rts = False; ser.dtr = False


SHOW_NODE = re.compile(r"\| NODE NODE run=\S+ .*boot_us=(\d+)")
SHOW_SLEEP = re.compile(r"\| NODE SLEEP on=\d+ wake=(\w+) .*now_ms=(\d+)")


def reset_problem(text, max_up_ms):
    """None when the node's last show reply proves a fresh cold boot (wake=boot, up no longer than max_up_ms), else the reason."""
    boot_us, reply = None, None
    for ln in text.splitlines():
        m = SHOW_NODE.search(ln)
        if m:
            boot_us = int(m.group(1))       # the node prints boot_ms * 1000 as a 32-bit number: it wraps every 4294.967 s
            continue
        m = SHOW_SLEEP.search(ln)
        if m and boot_us is not None:       # so compare in the same 32-bit microsecond space (A-54)
            reply = (m.group(1), ((int(m.group(2)) * 1000 - boot_us) & 0xFFFFFFFF) // 1000)
    if reply is None:
        return "the node did not answer show after the reset"
    if reply[0] != "boot":
        return "the node woke from sleep (wake=%s), it was not reset" % reply[0]
    if reply[1] > max_up_ms:
        return "the node has been up %d ms, longer than the %d ms since the reset: the reset did not reach it" % (reply[1], max_up_ms)
    return None


def expected_fw():
    import node_build
    return node_build.fw_identity()


def node_reader(port, cap, stop, holder):
    """Reads the node's lines into the log and reopens the port whenever it comes back: a sleeping node's USB port disappears."""
    import serial
    ser = holder.get("ser")
    buf = b""
    while not stop.is_set():
        if ser is None:
            try:
                ser = serial.Serial(); ser.port = port; ser.baudrate = 115200; ser.timeout = 0.2; ser.dtr = False; ser.rts = False; ser.open()
                holder["ser"] = ser
                cap.note("NODEPORT open")
            except Exception:
                ser = None; holder["ser"] = None
                time.sleep(0.3)
                continue
        try:
            chunk = ser.read(256)
        except Exception:
            try:
                ser.close()
            except Exception:
                pass
            ser = None; holder["ser"] = None; buf = b""
            cap.note("NODEPORT closed (node asleep or restarting)")
            continue
        if not chunk:
            continue
        buf += chunk
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            cap.note("NODE " + raw.decode("utf-8", "replace").rstrip("\r"))


def cmd_capture(a):
    path = log_file(a)
    if os.path.exists(path):
        sys.exit("STOP: %s already exists (a log is never appended to). Move it to test_logs/invalid/ or change HEAD." % path)
    header = logger.header_lines(a.tc, a.waived) + ["via=observe.py", "observer_port=%s" % a.port] + a.header
    expect_fw = None
    if a.node and a.node_reset and not a.no_fw_check:
        expect_fw = expected_fw()
        if expect_fw[2]:
            sys.exit("STOP: common/ or firmware/node_sense differ from HEAD. Commit them, rebuild and flash, then run (process rule 2).")
        header += ["fw_check=required", "fw_expected common=%08x node=%08x" % expect_fw[:2]]
    elif a.node:
        header.append("fw_check=skipped")
    if a.node:
        header.append("node_port=%s" % a.node)
        if a.node_plan:
            header += ["plan %g %s" % (t, c) for t, c in load_plan(a.node_plan)]
    cap = logger.Capture(a.port, a.baud, path, header)
    print("logging to", path)
    inj = None
    holder = {"ser": None}
    stop = threading.Event()
    stalled = threading.Event()
    fw_msg = None
    try:
        time.sleep(0.5)
        cap.send("RESET"); time.sleep(0.3)
        if a.filter:
            cap.send("FILTER %d" % a.filter); time.sleep(0.3)
        threading.Thread(target=observer_watchdog, args=(path, cap, stop, stalled), daemon=True).start()
        t0 = time.time()
        if a.node:                                          # the node under test: its lines go into the same log as NODE ...
            threading.Thread(target=node_reader, args=(a.node, cap, stop, holder), daemon=True).start()
            if a.node_reset:                                # the node boots INSIDE the capture: its 25 s boot grace starts now
                if a.wake_wait > 5.0:                       # a node that sleeps without a timer has no USB port: the button wakes it
                    print("\a>>> PRESS AND HOLD THE BUTTON NOW and keep it pressed (waiting up to %d s for the node to stay awake)" % a.wake_wait)
                    sys.stdout.flush()
                wait_port_stable(holder, a.wake_wait)         # the reset is only sent to a node that has stayed awake
                RESET_NOTE = "NODE RESET by RTS pulse"
                try:
                    reset_pulse(holder["ser"])
                    t_reset = time.time()
                    cap.note(RESET_NOTE)
                except Exception:
                    t_reset = None
                    cap.note("NODE RESET FAILED (no node port): the run is invalid")
                time.sleep(2.5)                             # the node's USB port goes away and comes back
                if expect_fw:                               # process rule 2: the flashed firmware must be the one of HEAD
                    # The buffered FW line of the cold boot can be printed before the port is reopened, so ask with show:
                    # its reply carries the FW line and proves the reset reached the node (wake=boot, up since the reset).
                    t_f = time.time(); rp = "the node did not answer show after the reset"; problem = None
                    if t_reset is not None and send_node(holder, "show", cap, stalled):
                        while time.time() - t_f < 25.0 and not stalled.is_set():
                            try:
                                after = open(path, encoding="utf-8").read().split(RESET_NOTE, 1)[-1]
                            except OSError:
                                after = ""
                            rp = reset_problem(after, int((time.time() - t_reset) * 1000) + 1000)
                            if rp != "the node did not answer show after the reset":   # the reply is complete (the SLEEP line comes last)
                                problem = None if rp else fw_problem(parse_fw(after), expect_fw)
                                break
                            time.sleep(0.5)
                    if t_reset is not None and rp:
                        fw_msg = "NODE RESET FAILED: %s" % rp
                        cap.note(fw_msg)
                        stalled.set()
                    elif problem:
                        fw_msg = "FIRMWARE MISMATCH: %s" % problem
                        cap.note(fw_msg)
                        stalled.set()
                    elif a.wake_wait > 5.0:
                        print("\a>>> Node reset and firmware verified. Keep the button PRESSED until a prompt tells you to release it."); sys.stdout.flush()
                t0 = time.time()
            plan = [(a.node_after + i * a.node_gap, c) for i, c in enumerate(a.node_send)]
            if a.node_plan:
                plan += load_plan(a.node_plan)
            plan.sort(key=lambda x: x[0])
            for t_cmd, c in plan:
                if not wait_until(t0 + t_cmd, stalled):
                    break
                if c.startswith("say "):
                    print("\a>>> %s   (t=%g s)" % (c[4:], t_cmd)); sys.stdout.flush()
                    cap.note("PROMPT %s" % c[4:])
                    continue
                if c == "reset":                            # tools/powercut.py plans (TC-75, TC-11): a reset at a seeded time
                    send_reset(holder, cap, stalled)
                    continue
                cap.note("NODETX %s" % c)
                send_node(holder, c, cap, stalled)
        if a.injector:
            import serial
            inj = serial.Serial(); inj.port = a.injector; inj.baudrate = 115200; inj.timeout = 0.2
            inj.dtr = False; inj.rts = False; inj.open()
            time.sleep(max(0.0, a.adv_after - (time.time() - t0)))
            cap.note("INJECT ADV %s" % a.adv)
            inj.write(("ADV %s\n" % a.adv).encode())
            parts = a.adv.split()
            wait = (int(parts[1]) * int(parts[2]) / 1000.0 if len(parts) >= 3 else 5.0) + 10.0   # all bursts plus 10 s, never longer
            t_cmd = time.time()
            got_ok = False
            while time.time() < t_cmd + wait:
                ln = inj.readline().decode("utf-8", "replace").strip()
                if ln:
                    print("injector:", ln)
                    cap.note("INJECTOR %s" % ln)
                    got_ok = got_ok or ln.startswith("OK")
                if ln.startswith("DONE") or ln.startswith("ERR"):
                    break
                if not got_ok and time.time() > t_cmd + 5.0:
                    print("STOP waiting: the injector gave no OK within 5 s")
                    cap.note("INJECTOR no OK within 5 s")
                    break
        wait_until(t0 + a.seconds, stalled)
        try:
            cap.send("TIME"); time.sleep(0.3)
            cap.send("STAT"); time.sleep(0.5)
        except Exception:
            cap.note("final TIME and STAT could not be sent (the observer port is gone)")
    except KeyboardInterrupt:
        pass
    finally:
        stop.set()
        time.sleep(0.5)
        if holder.get("ser"):
            try:
                holder["ser"].close()
            except Exception:
                pass
        if inj:
            inj.close()
        cap.close()
    if fw_msg:
        fix = "check the node's USB cable and port" if fw_msg.startswith("NODE RESET FAILED") else "flash the firmware of HEAD"
        print(fw_msg + ". The run was stopped before it started; %s and try again (the log is not evidence)." % fix)
        return 3
    if stalled.is_set():
        print("OBSERVER STALLED: the capture was stopped early, the log is not usable as evidence (move it to test_logs/invalid/)")
        return 2
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("capture")
    c.add_argument("--port", required=True); c.add_argument("--baud", type=int, default=115200)
    c.add_argument("--tc", required=True); c.add_argument("--seconds", type=float, default=60)
    c.add_argument("--filter", type=int, default=0); c.add_argument("--waived", action="store_true")
    c.add_argument("--header", action="append", default=[]); c.add_argument("--log")
    c.add_argument("--injector"); c.add_argument("--adv"); c.add_argument("--adv-after", type=float, default=3.0)
    c.add_argument("--node", help="COM port of the node under test (hook commands are sent to it)")
    c.add_argument("--node-send", action="append", default=[], help="a hook line for the node; repeat for several")
    c.add_argument("--node-after", type=float, default=5.0, help="seconds after the start before the first node command")
    c.add_argument("--node-gap", type=float, default=1.0, help="seconds between node commands")
    c.add_argument("--node-plan", help="file of timed hook lines made by the plan command")
    c.add_argument("--wake-wait", type=float, default=5.0, help="seconds to wait for the node's USB port before the reset (use 60 for a node that sleeps without a timer)")
    c.add_argument("--no-fw-check", action="store_true", help="skip the firmware identity check (never for evidence runs)")
    c.add_argument("--node-reset", action="store_true", help="reset the node with an RTS pulse at the start, so its boot grace is inside the capture")
    for name in ("analyze", "calibrate"):
        p = sub.add_parser(name)
        p.add_argument("--tc", required=True); p.add_argument("--log")
        p.add_argument("--len", type=int, default=21); p.add_argument("--prefix", default="14ffffff")
        p.add_argument("--gap", type=float, default=0.2, help="seconds: PDUs closer than this belong to one burst")
        p.add_argument("--spacing", type=int, default=500)
        if name == "analyze":
            p.add_argument("--copies", type=int, default=5); p.add_argument("--tol", type=int, default=50)
            p.add_argument("--quiet", type=float, default=10.0); p.add_argument("--expect-tx", type=int)
            p.add_argument("--need-marks", action="store_true", help="the log must hold the node's MARK COPY lines (A-20)")
            p.add_argument("--node-id", type=int, help="TC-01: every transmission must carry this node_id")
            p.add_argument("--cycles", type=int, default=0, help="TC-07: boot event then N x (remove, wear); counter +1 and the state alternates")
            p.add_argument("--heartbeats", type=int, default=0, help="TC-10: N heartbeats after the events")
    pl = sub.add_parser("plan")
    pl.add_argument("--cycles", type=int, default=20); pl.add_argument("--hold", type=float, default=6.0)
    pl.add_argument("--first", type=float, default=32.0); pl.add_argument("--grace", type=int, default=2)
    pl.add_argument("--hb", type=int, default=60); pl.add_argument("--heartbeats", type=int, default=20)
    pl.add_argument("--out", required=True)
    r = sub.add_parser("result")
    r.add_argument("--tc", required=True); r.add_argument("--result", required=True, choices=["PASS", "FAIL", "BLOCKED"])
    a = ap.parse_args()
    if a.cmd == "capture" and bool(a.injector) != bool(a.adv):
        ap.error("--injector and --adv go together")
    return {"capture": cmd_capture, "analyze": cmd_analyze, "calibrate": cmd_calibrate, "result": cmd_result, "plan": cmd_plan}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
