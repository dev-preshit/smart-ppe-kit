"""S4: node_build.py firmware identity and the flash-port guard (A-23, process rule 2)."""
import io, contextlib, os, sys, tempfile, unittest
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import node_build as nb


class FwIdentity(unittest.TestCase):
    def test_outside_a_git_checkout_the_identity_is_dirty_and_zero(self):
        d = tempfile.mkdtemp()
        self.assertEqual(nb.fw_identity(d), (0, 0, 1))

    def test_identity_defines_reach_the_build_options_and_the_output(self):
        with contextlib.redirect_stdout(io.StringIO()) as out:
            self.assertEqual(nb.main(["--type", "foot", "--hooks", "--dry-run"]), 0)
        text = out.getvalue()
        self.assertIn("firmware id: common=", text)
        for d in ("-DPPE_FW_COMMON=0x", "-DPPE_FW_NODE=0x", "-DPPE_FW_DIRTY="):
            self.assertIn(d, text)

    def test_build_opt_lines_are_unchanged_by_the_identity(self):
        self.assertEqual(nb.build_opt_lines("foot", False), ["-DPPE_BUILD_NODE_TYPE=0"])


class PortGuard(unittest.TestCase):
    def test_only_the_s4_bench_ports_may_be_flashed(self):
        for port in ("COM4", "COM7", "COM3", "COM12"):
            with contextlib.redirect_stderr(io.StringIO()) as err:
                with self.assertRaises(SystemExit) as cm:
                    nb.main(["--type", "foot", "--upload", port, "--dry-run"])
            self.assertEqual(cm.exception.code, 2)
            self.assertIn("refusing to flash " + port, err.getvalue())

    def test_the_allowed_ports_are_the_node_and_the_observer(self):
        self.assertEqual(nb.ALLOWED_UPLOAD_PORTS, ("COM9", "COM6"))


if __name__ == "__main__":
    unittest.main()
