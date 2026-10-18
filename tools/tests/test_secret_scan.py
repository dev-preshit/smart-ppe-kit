import base64, os, random, subprocess, sys, tempfile, unittest
HERE = os.path.dirname(__file__)
SCAN = os.path.join(HERE, "..", "secret_scan.py")
def sh(cwd, *a): return subprocess.run(a, cwd=cwd, capture_output=True, text=True, check=True)
def scan(repo, key=None):
    args = [sys.executable, SCAN, "--repo", repo] + (["--key-file", key] if key else [])
    r = subprocess.run(args, capture_output=True, text=True); return r.returncode, r.stdout
class T(unittest.TestCase):
    def setUp(self):
        self.d = tempfile.mkdtemp(); self.repo = os.path.join(self.d, "repo"); os.makedirs(self.repo)
        sh(self.repo, "git", "init", "-q", "-b", "main"); sh(self.repo, "git", "config", "user.email", "t@t"); sh(self.repo, "git", "config", "user.name", "t")
        self.key = bytes(random.Random(99).getrandbits(8) for _ in range(32))
        self.keyfile = os.path.join(self.d, "vest.key"); self._w(self.keyfile, self.key, "wb")
        self.put("a.txt", "clean\n"); self.commit("init")
    def commit(self, msg): sh(self.repo, "git", "add", "-A"); sh(self.repo, "git", "commit", "-q", "-m", msg)
    def _w(self, path, data, mode="w"):
        with open(path, mode) as f: f.write(data)
    def put(self, name, data, mode="w"): self._w(os.path.join(self.repo, name), data, mode)
    def test_clean_repo_passes(self): self.assertEqual(scan(self.repo, self.keyfile)[0], 0)
    def _form(self, name, text, mode="w"):
        self.put(name, text, mode); self.commit("add " + name)
        rc, out = scan(self.repo, self.keyfile); self.assertEqual(rc, 1, name + "\n" + out)
        self.assertNotIn(self.key.hex(), out)                 # the key is never echoed
        sh(self.repo, "git", "rm", "-q", name); self.commit("remove " + name)
        rc, out = scan(self.repo, self.keyfile); self.assertEqual(rc, 1, "must still be found in history: " + name)
    def test_hex(self): self._form("k1.txt", "key=" + self.key.hex() + "\n")
    def test_hex_upper(self): self._form("k1u.txt", self.key.hex().upper())
    def test_spaced(self): self._form("k2.txt", " ".join("%02X" % b for b in self.key))
    def test_colon(self): self._form("k2c.txt", ":".join("%02x" % b for b in self.key))
    def test_c_array_wrapped(self):
        body = ",\n  ".join(", ".join("0x%02X" % b for b in self.key[i:i + 8]) for i in range(0, 32, 8))
        self._form("k3.h", "static const uint8_t K[32] = {\n  " + body + "};\n")
    def test_base64(self): self._form("k4.txt", base64.b64encode(self.key).decode())
    def test_decimal(self): self._form("k5.txt", "{" + ", ".join(str(b) for b in self.key) + "}")
    def test_raw_binary_blob(self):
        self.put("fw.bin", b"\x00" * 10 + self.key + b"\xff" * 10, "wb"); self.commit("bin")
        self.assertEqual(scan(self.repo, self.keyfile)[0], 1)
    def test_key_in_ignored_worktree_file_found(self):
        self.put(".gitignore", "secrets.h\n"); self.commit("ignore"); self.put("secrets.h", "#define K \"%s\"" % self.key.hex())
        self.assertEqual(scan(self.repo, self.keyfile)[0], 1)
    def test_key_in_commit_message_found(self):
        self.put("b.txt", "x"); sh(self.repo, "git", "add", "-A"); sh(self.repo, "git", "commit", "-q", "-m", "oops " + self.key.hex())
        self.assertEqual(scan(self.repo, self.keyfile)[0], 1)
    def test_other_branch_history_found(self):
        sh(self.repo, "git", "switch", "-q", "-c", "side"); self.put("s.txt", self.key.hex()); self.commit("side")
        sh(self.repo, "git", "switch", "-q", "main"); self.assertEqual(scan(self.repo, self.keyfile)[0], 1)
    def test_pattern_mode_flags_unknown_key_allows_test_key(self):
        self.put("t.h", ",".join("0x%02X" % i for i in range(32))); self.put("z.h", ",".join(["0x00"] * 32)); self.commit("allowed")
        self.assertEqual(scan(self.repo)[0], 0)
        self.put("r.txt", self.key.hex()); self.commit("random key"); self.assertEqual(scan(self.repo)[0], 1)
    def test_wrong_key_not_flagged(self):
        self.put("k.txt", bytes(range(1, 33)).hex()); self.commit("other"); self.assertEqual(scan(self.repo, self.keyfile)[0], 0)
if __name__ == "__main__": unittest.main()
