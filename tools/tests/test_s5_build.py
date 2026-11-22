"""Tests for the S5 hub firmware identity check (tools/s5_build.py and s5_bench fw_*). No git, no hardware."""
import os
import sys
import unittest
from unittest import mock

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import s5_bench as bench  # noqa: E402
import s5_build  # noqa: E402

HEAD = (0x1234abcd, 0x0badf00d, 0)
GOOD = "2026-11-23T10:00:00.000 | FW hub=1234abcd common=0badf00d dirty=0 hooks=1\nDUMP end\n"


class FakeSerial:
    def __init__(self, reply):
        self.reply, self.sent, self.closed = reply.encode(), b"", False

    def write(self, b):
        self.sent += b

    def read(self, n):
        out, self.reply = self.reply[:n], self.reply[n:]
        return out

    def close(self):
        self.closed = True


class ParseAndCompare(unittest.TestCase):
    def test_parse_takes_the_last_line(self):
        text = "FW hub=00000001 common=00000002 dirty=1 hooks=1\n" + GOOD
        self.assertEqual(bench.parse_fw(text), (0x1234abcd, 0x0badf00d, 0, 1))

    def test_no_line_is_none(self):
        self.assertIsNone(bench.parse_fw("HUB boot (S3 receive path)\nDUMP end\n"))

    def test_match_is_ok(self):
        self.assertIsNone(bench.fw_problem(bench.parse_fw(GOOD), HEAD))

    def test_missing_line_stops(self):
        self.assertIn("no FW line", bench.fw_problem(None, HEAD))

    def test_other_firmware_stops(self):
        self.assertIn("HEAD is", bench.fw_problem((0x1234abce, 0x0badf00d, 0, 1), HEAD))
        self.assertIn("HEAD is", bench.fw_problem((0x1234abcd, 0x0badf00e, 0, 1), HEAD))

    def test_dirty_build_stops(self):
        self.assertIn("dirty", bench.fw_problem((0x1234abcd, 0x0badf00d, 1, 1), HEAD))

    def test_dirty_tree_stops(self):
        self.assertIn("uncommitted", bench.fw_problem(bench.parse_fw(GOOD), (0x1234abcd, 0x0badf00d, 1)))

    def test_release_build_stops(self):
        self.assertIn("release", bench.fw_problem((0x1234abcd, 0x0badf00d, 0, 0), HEAD))

    def test_default_macros_never_match(self):          # a build without s5_build.py prints zeros and dirty=1
        self.assertIsNotNone(bench.fw_problem((0, 0, 1, 1), HEAD))


class Query(unittest.TestCase):
    def test_query_sends_dump_and_reads_until_dump_end(self):
        fake = FakeSerial(GOOD + "late line\n")
        with mock.patch.object(bench.time, "sleep"):
            text = bench.fw_query("COM3", timeout_s=2, serial_factory=lambda p: fake)
        self.assertEqual(fake.sent, b"dump\n")
        self.assertTrue(fake.closed)
        self.assertIn("DUMP end", text)

    def test_check_stops_the_driver_on_a_mismatch(self):
        with mock.patch.object(s5_build, "fw_identity", return_value=HEAD), \
             mock.patch.object(bench, "fw_query", return_value="FW hub=00000000 common=00000000 dirty=1 hooks=1\n"):
            with self.assertRaises(SystemExit):
                bench.fw_check("COM3")

    def test_check_returns_header_on_a_match(self):
        with mock.patch.object(s5_build, "fw_identity", return_value=HEAD), mock.patch.object(bench, "fw_query", return_value=GOOD):
            self.assertEqual(bench.fw_check("COM3"), "fw_check=ok hub=1234abcd common=0badf00d")


class Build(unittest.TestCase):
    def test_upload_refuses_other_ports(self):
        for port in ("COM4", "COM8", "COM9"):
            self.assertEqual(s5_build.main(["upload", "--port", port]), 2)

    def test_upload_refuses_a_stale_build(self):
        with mock.patch.object(s5_build, "fw_identity", return_value=HEAD), \
             mock.patch("builtins.open", mock.mock_open(read_data="hub=00000000 common=00000000 dirty=0\n")), \
             mock.patch.object(s5_build.subprocess, "run") as run:
            self.assertEqual(s5_build.main(["upload", "--port", "COM3"]), 2)
            run.assert_not_called()

    def test_build_refuses_a_dirty_tree(self):
        with mock.patch.object(s5_build, "fw_identity", return_value=(1, 2, 1)), mock.patch.object(s5_build.subprocess, "run") as run:
            self.assertEqual(s5_build.main(["build"]), 2)
            run.assert_not_called()

    def test_fmt(self):
        self.assertEqual(s5_build.fmt(HEAD), "hub=1234abcd common=0badf00d dirty=0")


if __name__ == "__main__":
    unittest.main()
