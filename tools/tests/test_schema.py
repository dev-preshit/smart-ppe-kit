import copy, json, os, unittest
from jsonschema import Draft202012Validator
S = json.load(open(os.path.join(os.path.dirname(__file__), "..", "schema", "uplink.schema.json")))
V = Draft202012Validator(S)
B = dict(vest_id=1, hub_seq=3, age_ms=0)
def m(t, **k): d = dict(msg_type=t, **B); d.update(k); return d
NODES = [dict(node_id=i, state="WORN", battery=90, flags=64, last_seen_age_s=10) for i in range(1, 6)]
DN = [dict(node_id=i, hmac_fail=0, dup_count=4, replay_count=0, gap_count=1, accepted=9) for i in range(1, 6)]
DIAG = dict(rx_total=50, drop_len=1, drop_ver=0, drop_vest=0, drop_node=0, drop_state=0, scan_quiet_60s=0, nodes=DN)
VALID = {
 "PACKET": m("PACKET", node_id=3, state="WORN", pkt_type="EVENT", battery=90, flags=96, counter=258),
 "PACKET_batt_na": m("PACKET", node_id=1, state="NOT_WORN", pkt_type="HEARTBEAT", battery=255, flags=64, counter=1),
 "NODE_OFFLINE": m("NODE_OFFLINE", node_id=2, state="OFFLINE", detail=dict(last_seen_age_s=781)),
 "NODE_REBOOT": m("NODE_REBOOT", node_id=2, state="WORN", detail=dict(missed=63)),
 "COMPLIANCE_ok": m("COMPLIANCE", state="COMPLIANT", detail=dict(nodes_not_ok=[])),
 "COMPLIANCE_bad": m("COMPLIANCE", state="NON_COMPLIANT", detail=dict(nodes_not_ok=[1, 5])),
 "COUNTER_ANOMALY": m("COUNTER_ANOMALY", node_id=4, detail=dict(received_counter=5, last_counter=900)),
 "COUNTER_RESET_node": m("COUNTER_RESET", detail=dict(scope="node", node_id=4)),
 "COUNTER_RESET_all": m("COUNTER_RESET", detail=dict(scope="all")),
 "HUB_BOOT": dict(msg_type="HUB_BOOT", vest_id=1, hub_seq=0, age_ms=0, detail=dict(reset_reason="POWERON", fw="s3-abc1234")),
 "HUB_FAULT": m("HUB_FAULT", detail=dict(code="NO_KEY")),
 "HUB_STATUS": m("HUB_STATUS", detail=dict(uptime_s=60, nodes=NODES, diag=DIAG, buffer_fill=0, dropped=0, superseded=0)),
}
def bad(name, base, fn):
    d = copy.deepcopy(VALID[base]); fn(d); return name, d
INVALID = [
 bad("unknown msg_type", "PACKET", lambda d: d.update(msg_type="NOPE")),
 bad("missing hub_seq", "PACKET", lambda d: d.pop("hub_seq")),
 bad("negative age", "PACKET", lambda d: d.update(age_ms=-1)),
 bad("vest 0", "PACKET", lambda d: d.update(vest_id=0)),
 bad("vest 255", "PACKET", lambda d: d.update(vest_id=255)),
 bad("node 6", "PACKET", lambda d: d.update(node_id=6)),
 bad("node 0", "PACKET", lambda d: d.update(node_id=0)),
 bad("counter over uint32", "PACKET", lambda d: d.update(counter=2**32)),
 bad("battery 101", "PACKET", lambda d: d.update(battery=101)),
 bad("PACKET state UNKNOWN", "PACKET", lambda d: d.update(state="UNKNOWN")),
 bad("PACKET missing pkt_type", "PACKET", lambda d: d.pop("pkt_type")),
 bad("PACKET with detail", "PACKET", lambda d: d.update(detail={})),
 bad("extra top-level field", "PACKET", lambda d: d.update(extra=1)),
 bad("string counter", "PACKET", lambda d: d.update(counter="5")),
 bad("OFFLINE wrong state", "NODE_OFFLINE", lambda d: d.update(state="WORN")),
 bad("OFFLINE no detail", "NODE_OFFLINE", lambda d: d.pop("detail")),
 bad("REBOOT missing missed", "NODE_REBOOT", lambda d: d["detail"].pop("missed")),
 bad("COMPLIANT lists nodes", "COMPLIANCE_ok", lambda d: d["detail"].update(nodes_not_ok=[1])),
 bad("NON_COMPLIANT empty list", "COMPLIANCE_bad", lambda d: d["detail"].update(nodes_not_ok=[])),
 bad("duplicate not-ok nodes", "COMPLIANCE_bad", lambda d: d["detail"].update(nodes_not_ok=[1, 1])),
 bad("COMPLIANCE node state", "COMPLIANCE_ok", lambda d: d.update(state="WORN")),
 bad("anomaly missing last_counter", "COUNTER_ANOMALY", lambda d: d["detail"].pop("last_counter")),
 bad("reset node scope without node_id", "COUNTER_RESET_node", lambda d: d["detail"].pop("node_id")),
 bad("reset bad scope", "COUNTER_RESET_all", lambda d: d["detail"].update(scope="vest")),
 bad("HUB_BOOT hub_seq not 0", "HUB_BOOT", lambda d: d.update(hub_seq=5)),
 bad("HUB_BOOT missing fw", "HUB_BOOT", lambda d: d["detail"].pop("fw")),
 bad("HUB_FAULT bad code", "HUB_FAULT", lambda d: d["detail"].update(code="OOPS")),
 bad("STATUS 4 nodes", "HUB_STATUS", lambda d: d["detail"].update(nodes=NODES[:4])),
 bad("STATUS buffer_fill 101", "HUB_STATUS", lambda d: d["detail"].update(buffer_fill=101)),
 bad("STATUS missing superseded", "HUB_STATUS", lambda d: d["detail"].pop("superseded")),
 bad("STATUS diag missing drop_state", "HUB_STATUS", lambda d: d["detail"]["diag"].pop("drop_state")),
]
class T(unittest.TestCase):
    def test_schema_itself_valid(self): Draft202012Validator.check_schema(S)
    def test_every_msg_type_has_a_valid_example(self):
        self.assertEqual({k.split("_")[0] + ("_" + k.split("_")[1] if k.split("_")[0] in ("NODE", "COUNTER", "HUB") else "") for k in VALID},
                         set(S["properties"]["msg_type"]["enum"]))
    def test_valid_examples(self):
        for n, d in VALID.items():
            errs = list(V.iter_errors(d)); self.assertEqual(errs, [], "%s: %s" % (n, [e.message for e in errs]))
    def test_invalid_examples_rejected(self):
        for n, d in INVALID:
            self.assertTrue(list(V.iter_errors(d)), "accepted but should be rejected: " + n)
    def test_hub_seq_bounds(self):
        d = copy.deepcopy(VALID["PACKET"]); d["hub_seq"] = 2**32 - 1; self.assertEqual(list(V.iter_errors(d)), [])
if __name__ == "__main__": unittest.main()
