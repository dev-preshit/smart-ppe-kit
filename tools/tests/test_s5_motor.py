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


if __name__ == "__main__":
    unittest.main()
