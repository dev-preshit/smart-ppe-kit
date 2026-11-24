import os, sys, tempfile, unittest
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import powercut, observe

R = ("reset",)
def C(n): return ("ctr", n)


class T(unittest.TestCase):
    def test_plan_is_seeded_and_has_the_requested_resets(self):
        a, _ = powercut.make_plan(1000, 75, "random"); b, _ = powercut.make_plan(1000, 75, "random")
        self.assertEqual(a, b)
        self.assertNotEqual(a, powercut.make_plan(1000, 76, "random")[0])
        self.assertEqual(sum(1 for _, c in a if c == "reset"), 1000)
        ts = [t for t, _ in a]
        self.assertTrue(all(ts[i] <= ts[i + 1] for i in range(len(ts) - 1)))
        self.assertEqual(a[-1][1], "tx worn")
        early = sum(1 for i in range(len(a) - 1) if a[i][1] == "reset" and a[i + 1][1] == "reset")
        self.assertTrue(200 < early < 400, early)                 # about 30 % cut inside the boot NVS write

    def test_after_mode_sends_one_tx_per_reset(self):
        p, _ = powercut.make_plan(100, 11, "after")
        self.assertEqual(sum(1 for _, c in p if c == "reset"), 100)
        self.assertEqual(sum(1 for _, c in p if c == "tx worn"), 101)

    def test_resets_with_block_jumps_and_repeated_copies_pass(self):
        ev = [C(65), C(65), C(65), R, C(65), R, R, C(193), C(193), R, C(257)]   # a late copy of 65 after the reset is fine
        ok, lines = powercut.analyze(ev, 0, 4)
        self.assertTrue(ok, lines); self.assertIn("RESULT PASS", lines[-1])

    def test_a_repeated_or_decreasing_counter_fails(self):
        ok, lines = powercut.analyze([C(65), R, C(129), C(65), R, C(300)], 0, 2); self.assertFalse(ok)
        self.assertTrue(any("came back" in l for l in lines))
        ok, lines = powercut.analyze([C(129), R, C(100)], 0, 1); self.assertFalse(ok)
        self.assertTrue(any("decreased" in l for l in lines))

    def test_a_step_of_one_across_a_reset_means_the_reset_did_not_reach_the_node(self):
        ok, lines = powercut.analyze([C(65), R, C(66)], 0, 1); self.assertFalse(ok)
        self.assertTrue(any("INVALID" in l for l in lines))

    def test_wrong_reset_count_lost_packets_and_failure_notes_fail(self):
        self.assertFalse(powercut.analyze([C(1), R, C(65)], 0, 2)[0])
        self.assertFalse(powercut.analyze([C(1), R, C(65)], 3, 1)[0])
        self.assertFalse(powercut.analyze([C(1), R, ("bad", "NODE RESET FAILED (plan reset)"), C(65)], 0, 1)[0])
        self.assertFalse(powercut.analyze([R], 0, 1)[0])

    def test_log_reader_takes_plan_resets_and_node_packets_after_the_observer_reset(self):
        def adv(ctr, node=3):
            return (bytearray([0x14, 0xFF, 0xFF, 0xFF, 0x60, 1, node, 1, 255]) + ctr.to_bytes(4, "little") + bytes(8)).hex()
        lines = ["x | PKT 1.000000 21 3 -60 %s" % adv(5), "x | OK reset",
                 "x | PKT 2.000000 21 3 -60 %s" % adv(65), "x | " + observe.PLAN_RESET_NOTE,
                 "x | PKT 3.000000 21 3 -60 %s" % adv(129), "x | PKT 3.100000 21 3 -60 %s" % adv(9, node=4),
                 "x | NODE RESET by RTS pulse", "x | STAT seen=9 printed=9 lost=0 filter=21"]
        d = tempfile.mkdtemp(); p = os.path.join(d, "TC-75.log")
        open(p, "w", encoding="utf-8").write("\n".join(lines) + "\n")
        ev, lost = powercut.read_events(p, node_id=3)
        self.assertEqual(ev, [C(65), R, C(129)]); self.assertEqual(lost, 0)


if __name__ == "__main__":
    unittest.main()
