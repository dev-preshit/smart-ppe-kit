#!/usr/bin/env python3
"""powercut.py - SC-08 (A-17): seeded node resets for TC-75 (counter durability, NFR-06) and TC-11 step 2 (forced resets).
There is no power switch (EQ-05): a "cut" is the RTS reset pulse on the node's USB port, sent by observe.py capture.

  plan    --out plan.txt --count 1000 --seed 75 [--mode random|after]    seeded reset/tx plan for observe.py capture --node-plan
  analyze --tc TC-75 [--log PATH] --count 1000                           PASS = on-air counter log strictly increasing

Plan modes (each cycle starts with a reset; the node must not sleep, a FOOT build in its 25 s boot grace never does):
  random  TC-75: 30 % of the cycles cut again 0.3 to 2.5 s after the reset (inside the cold-boot counter-block NVS write,
          N-04), 70 % send 'tx worn' 3 s after the reset and cut 0 to 2.6 s later (inside the 5-copy transmit window).
  after   TC-11 step 2: 'tx worn' 3 s after each reset, the next reset 3 s after that (the sequence is complete).
Run:  python tools/observe.py capture --port COM6 --tc TC-75 --node COM9 --node-reset --node-plan plan.txt --seconds <printed>
Analysis (one 'copy' = one burst, copies share a counter): logical packets are runs of PDUs with the same counter.
  - a counter value never comes back after another one, and the logical counters are strictly increasing (the pass rule);
  - proof that each reset reached the node: the first new counter after a reset must jump by 2 or more (a cold boot takes a
    new counter block, N-04); a step of exactly 1 across a reset means the node was not restarted and the run is INVALID;
  - the number of plan resets in the log must equal --count, no lost packets, no failed command, reset or stalled observer."""
import argparse, os, random, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import observe

PORT_BACK_S = 3.0      # after a reset the node's USB port is back and its hook reader runs (TRY logs: under 2 s; margin)
SEQ_S = 2.6            # 5 copies 500 ms start to start = 2.0 s plus the last burst and margin
BAD_NOTES = ("OBSERVER STALLED", "NODE RESET FAILED", "NODETX FAILED", "FIRMWARE MISMATCH", "final TIME and STAT could not be sent")


def make_plan(count, seed, mode, first=2.0):
    """[(t, cmd)] with 'reset' and 'tx worn' lines; t in seconds from the start of the plan (after the verified first reset)."""
    rng = random.Random(seed)
    out, t = [], first
    for _ in range(count):
        out.append((t, "reset"))
        if mode == "after":
            out.append((t + PORT_BACK_S, "tx worn")); t += PORT_BACK_S + SEQ_S + 0.4
        elif rng.random() < 0.3:
            t += rng.uniform(0.3, 2.5)
        else:
            out.append((t + PORT_BACK_S, "tx worn")); t += PORT_BACK_S + rng.uniform(0.0, SEQ_S)
    out.append((t + PORT_BACK_S, "tx worn"))               # the first counter after the last reset
    return [(round(a, 3), c) for a, c in out], t + PORT_BACK_S


def cmd_plan(a):
    plan, last = make_plan(a.count, a.seed, a.mode)
    with open(a.out, "w", newline="\n") as f:
        f.write("# powercut.py plan: count=%d seed=%d mode=%s (observe.py capture --node-plan)\n" % (a.count, a.seed, a.mode))
        for t, c in plan:
            f.write("%g %s\n" % (t, c))
    print("plan written: %s, %d resets, seed %d, mode %s; run at least --seconds %d (last command %g s + copies + 15 s margin)"
          % (a.out, a.count, a.seed, a.mode, int(last + SEQ_S + 15) + 1, last))
    return 0


def read_events(path, node_id=None):
    """The log in file order: ('reset',) for each plan reset, ('ctr', n) for each matching PDU, ('bad', text) for failures."""
    ev, lost = [], 0
    with open(path, encoding="utf-8") as f:
        for ln in f:
            if "| OK reset" in ln:
                ev, lost = [], 0
                continue
            if observe.PLAN_RESET_NOTE in ln:
                ev.append(("reset",)); continue
            if any(b in ln for b in BAD_NOTES):
                ev.append(("bad", ln.split("| ", 1)[-1].strip())); continue
            m = observe.LOSS.search(ln)
            if m:
                lost = int(m.group(1) or m.group(2)); continue
            m = observe.PKT.search(ln)
            if m and int(m.group(3)) == 21 and m.group(6).startswith("14ffffff"):
                d = observe.dec(m.group(6))
                if node_id is None or d["node"] == node_id:
                    ev.append(("ctr", d["ctr"]))
    return ev, lost


def analyze(ev, lost, count):
    """-> (ok, lines). See the module text for the rules."""
    lines, problems = [], []
    resets = sum(1 for e in ev if e[0] == "reset")
    bad = [e[1] for e in ev if e[0] == "bad"]
    logical, seen, after_reset, firsts, cycles_with_pkt = [], set(), False, [], 0
    for e in ev:
        if e[0] == "reset":
            after_reset = True
            continue
        if e[0] != "ctr":
            continue
        c = e[1]
        if logical and c == logical[-1]:
            continue                                       # another PDU or copy of the same logical packet
        if c in seen:
            problems.append("counter %d came back after %d (repeated)" % (c, logical[-1]))
        elif logical and c < logical[-1]:
            problems.append("counter decreased: %d after %d" % (c, logical[-1]))
        if after_reset:
            cycles_with_pkt += 1
            firsts.append(c)
            if logical and c == logical[-1] + 1:
                problems.append("INVALID: counter %d follows %d by 1 across a reset: the reset did not restart the node" % (c, logical[-1]))
            after_reset = False
        seen.add(c); logical.append(c)
    if resets != count:
        problems.append("plan resets in the log: %d, expected %d" % (resets, count))
    if lost:
        problems.append("observer lost %d packets" % lost)
    for b in bad:
        problems.append("the capture reports: %s" % b)
    if not logical:
        problems.append("no packet from the node")
    lines.append("resets=%d (expected %d) logical_packets=%d resets_followed_by_a_packet=%d lost=%d" % (resets, count, len(logical), cycles_with_pkt, lost))
    if logical:
        lines.append("counters first=%d last=%d" % (logical[0], logical[-1]))
    lines.append("first counter after each reset (%d): %s" % (len(firsts), " ".join(str(x) for x in firsts[:50]) + (" ..." if len(firsts) > 50 else "")))
    lines += ["FINDING %s" % p for p in problems[:30]]
    if len(problems) > 30:
        lines.append("FINDING ... %d more" % (len(problems) - 30))
    ok = not problems
    lines.append("RESULT %s" % ("PASS" if ok else "FAIL"))
    return ok, lines


def cmd_analyze(a):
    path = observe.log_file(a)
    ev, lost = read_events(path, a.node_id)
    ok, lines = analyze(ev, lost, a.count)
    observe.emit(path, lines)
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("plan"); p.add_argument("--out", required=True); p.add_argument("--count", type=int, required=True)
    p.add_argument("--seed", type=int, required=True); p.add_argument("--mode", choices=("random", "after"), default="random")
    p.set_defaults(fn=cmd_plan)
    z = sub.add_parser("analyze"); z.add_argument("--tc", required=True); z.add_argument("--log"); z.add_argument("--count", type=int, required=True)
    z.add_argument("--node-id", type=int); z.set_defaults(fn=cmd_analyze)
    a = ap.parse_args()
    return a.fn(a)


if __name__ == "__main__":
    sys.exit(main())
