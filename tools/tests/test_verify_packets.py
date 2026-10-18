import hashlib, hmac, os, sys, tempfile, unittest
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import verify_packets as vp
K = vp.TEST_KEY
def pl(ctr, node=3, state=1, flags=0x60):
    b = bytes([flags, 1, node, state, 90]) + ctr.to_bytes(4, "little")
    return b + hmac.new(K, b, hashlib.sha256).digest()[:8]
def adv(p): return bytes([0x14, 0xFF, 0xFF, 0xFF]) + p
def cap(ctrs, copies=5, step_us=500_000):
    out, t = [], 1_000_000
    for c in ctrs:
        for k in range(copies): out.append((len(out) + 1, t + k * step_us, adv(pl(c)))); 
        t += 360_000_000
    return out
class T(unittest.TestCase):
    def test_a1_vector_verifies(self):
        a1 = bytes.fromhex("14FFFFFF60010301 5A02010000 3222111BD50A5B34".replace(" ", ""))
        r = vp.verify([(1, None, a1)], K, vest=1)
        self.assertTrue(r["ok"]); self.assertEqual(r["hmac_ok"], 1)
    def test_a2_payload_verifies(self):
        r = vp.verify([(1, None, bytes.fromhex("40010500 32 03010000 41B47A3B28822B6A".replace(" ", "")))], K)
        self.assertEqual(r["hmac_ok"], 1)
    def test_1000_packets_all_accepted(self):
        r = vp.verify(cap(range(1, 201)), K, strict=True)
        self.assertEqual((r["total"], r["hmac_ok"], r["logical"]), (1000, 1000, 200)); self.assertTrue(r["ok"])
    def test_bitflip_detected(self):
        c = cap([1]); n, t, b = c[0]; bb = bytearray(b); bb[-1] ^= 1; c[0] = (n, t, bytes(bb))
        r = vp.verify(c, K); self.assertEqual(r["hmac_fail"], 1); self.assertFalse(r["ok"])
    def test_wrong_key_fails(self):
        self.assertFalse(vp.verify(cap([1]), bytes(32))["ok"])
    def test_strict_copy_count_spacing_and_counter_step(self):
        self.assertFalse(vp.verify(cap([1], copies=4), K, strict=True)["ok"])
        self.assertFalse(vp.verify(cap([1], step_us=700_000), K, strict=True)["ok"])
        self.assertFalse(vp.verify(cap([1, 3]), K, strict=True)["ok"])
        self.assertTrue(vp.verify(cap([1, 3]), K, strict=False)["ok"])
    def test_cli_file_roundtrip(self):
        with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as f:
            for _, t, b in cap([1, 2]): f.write("%d,%s\n" % (t, b.hex()))
        self.assertEqual(vp.main([f.name, "--test-key", "--strict", "--vest", "1"]), 0); os.unlink(f.name)
if __name__ == "__main__": unittest.main()
