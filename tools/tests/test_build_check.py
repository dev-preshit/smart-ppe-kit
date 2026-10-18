import os, stat, sys, tempfile, unittest
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import build_check as bc
@unittest.skipIf(os.name == "nt", "fake CLI is a POSIX shell script")
class T(unittest.TestCase):
    def fake(self, core="3.0.7", nimble="2.5.1", compile_rc=0, elf=b"clean"):
        d = tempfile.mkdtemp(); p = os.path.join(d, "fakecli"); bp = os.path.join(d, "out"); os.makedirs(bp)
        with open(os.path.join(bp, "x.elf"), "wb") as f: f.write(elf)
        with open(p, "w") as f: f.write("""#!/bin/sh
case "$1 $2" in
 "version "*) echo "arduino-cli  Version: 1.0.0";;
 "core list") echo "ID          Installed Latest Name"; echo "esp32:esp32 %s     3.1.0  esp32";;
 "lib list") echo "Name            Installed Available Location"; echo "NimBLE-Arduino  %s     2.5.1 user";;
 "compile "*) for a in "$@"; do last=$a; done; bp=""; prev=""; for a in "$@"; do [ "$prev" = "--build-path" ] && bp="$a"; prev="$a"; done
   cp %s/x.elf "$bp/x.elf"; echo compiling; exit %d;;
esac
""" % (core, nimble, bp, compile_rc)); os.chmod(p, 0o755)
        t = os.path.join(d, "targets.txt")
        with open(t, "w") as f: f.write("n|tools|esp32:esp32:esp32c3\n")
        return p, t
    def test_pass(self):
        p, t = self.fake(); r = bc.check("2.3.10", t, cli=p); self.assertEqual(r["result"], "PASS", r)
    def test_wrong_core_fails(self):
        p, t = self.fake(core="3.1.0"); self.assertEqual(bc.check("2.3.10", t, cli=p)["result"], "FAIL")
    def test_wrong_nimble_fails(self):
        p, t = self.fake(nimble="2.3.0"); self.assertEqual(bc.check("2.3.10", t, cli=p)["result"], "FAIL")
    def test_ide_must_be_attested(self):
        p, t = self.fake(); self.assertEqual(bc.check(None, t, cli=p)["result"], "FAIL")
    def test_compile_error_fails(self):
        p, t = self.fake(compile_rc=1); self.assertEqual(bc.check("2.3.10", t, cli=p)["result"], "FAIL")
    def test_hooks_in_elf_fail(self):
        p, t = self.fake(elf=b"..setcounter.."); self.assertEqual(bc.check("2.3.10", t, cli=p)["result"], "FAIL")
    def test_missing_cli_blocked(self):
        self.assertEqual(bc.check("2.3.10", cli="no-such-cli-xyz")["result"], "BLOCKED")
    def test_no_targets_blocked(self):
        p, _ = self.fake(); d = tempfile.mkdtemp(); t = os.path.join(d, "t.txt")
        with open(t, "w") as f: f.write("# nothing\n")
        self.assertEqual(bc.check("2.3.10", t, cli=p)["result"], "BLOCKED")
if __name__ == "__main__": unittest.main()
