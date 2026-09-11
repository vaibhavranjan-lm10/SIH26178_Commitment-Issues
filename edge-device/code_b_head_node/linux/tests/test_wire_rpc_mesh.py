"""Pod report decoding (Code A wire v1), the RPC line grammar, mesh packets."""
import numpy as np
import pytest

from prahari_hn import mesh, rpc, wire


def make_report():
    return wire.Report(position=4, flags=wire.RF_RAIL_VALID, cycle=77, rail_mv=3298, xdcr_fault=0, xdcr_nopower=2,
                       xdcr_range=1,
                       primary=[wire.Channel(13, wire.F_VALID, 28450), wire.Channel(15, wire.F_VALID | wire.F_RANGE, 101000),
                                wire.Channel(22, wire.F_VALID | wire.F_STALE, 41000), wire.Channel(24, wire.F_VALID | wire.F_FAULT, 0),
                                wire.Channel(17, wire.F_VALID | wire.F_RAW, 512)],
                       derived=[wire.Channel(13, wire.F_VALID, 1585), wire.Channel(15, wire.F_VALID, -10000)])


def test_wire_round_trip_and_usability():
    buf = wire.encode_report(make_report())
    r = wire.decode_report(buf)
    assert r.position_name == "U" and r.cycle == 77 and r.rail_mv == 3298 and not r.wake
    by = {c.id: c for c in r.primary}
    assert by[13].usable and by[13].value == pytest.approx(28.45)
    assert by[15].usable and by[15].flags & wire.F_RANGE          # range-flagged: retained, flagged (§4.3)
    assert by[22].usable                                          # stale = held-over real measurement
    assert not by[24].usable and not by[17].usable                # fault / raw: never enters the column
    assert [c.id for c in r.derived] == [13, 15] and r.derived[1].value == -10.0


def test_wire_rejects_malformed():
    buf = wire.encode_report(make_report())
    with pytest.raises(wire.WireError):
        wire.decode_report(buf[:-1])
    with pytest.raises(wire.WireError):
        wire.decode_report(b"\x02" + buf[1:])
    bad = bytearray(buf); bad[14] = 99                             # primary[0].id (after the 14-byte header) = 99
    with pytest.raises(wire.WireError):
        wire.decode_report(bytes(bad))


def test_rpc_parse_mcu_lines():
    buf = wire.encode_report(make_report())
    ev = rpc.parse_line(f"$POD 5 {buf.hex()}\n")
    assert isinstance(ev, rpc.PodReportEvent) and ev.addr == 5 and ev.report == buf
    assert rpc.parse_line("$MISSING 4\n") == rpc.MissingEvent(4)
    assert rpc.parse_line("$SWEEP 12 2 3\n") == rpc.SweepEvent(12, 2, 3)
    assert rpc.parse_line("$TIME 1757400000123 locked\n") == rpc.TimeEvent(1757400000123, "locked")
    st = rpc.parse_line("$STAT sweeps=12 pods=3 pps=locked drift=-3ppm batt=12810mV\n")
    assert isinstance(st, rpc.StatEvent) and st.fields["batt"] == "12810mV"
    assert isinstance(rpc.parse_line("$NBR b701" + "00" * 18 + "\n"), rpc.NeighbourEvent)
    assert rpc.parse_line("PRAHARI head node MCU, node X\n") is None     # console noise
    with pytest.raises(rpc.RpcError):
        rpc.parse_line("$SWEEP 1 x 3")
    with pytest.raises(rpc.RpcError):
        rpc.parse_line("$BOGUS")


def test_rpc_outbound_grammar_matches_rpc_c():
    assert rpc.fmt_alert(2, 600) == "$ALERT 2 600\n" and rpc.fmt_alert(0) == "$ALERT 0\n"
    assert rpc.fmt_pods([1, 4, 5]) == "$PODS 1,4,5\n"
    assert rpc.fmt_cadence(300) == "$CADENCE 300\n" and rpc.fmt_arm(True) == "$ARM 1\n"
    assert rpc.fmt_emb(bytes(20)) == "$EMB " + "00" * 20 + "\n" and rpc.fmt_status() == "$STATUS\n"
    for bad in (lambda: rpc.fmt_alert(1, 0), lambda: rpc.fmt_alert(3, 1), lambda: rpc.fmt_pods([]),
                lambda: rpc.fmt_pods([0]), lambda: rpc.fmt_cadence(0), lambda: rpc.fmt_emb(bytes(33))):
        with pytest.raises(rpc.RpcError):
            bad()


def test_loopback_link():
    l = rpc.LoopbackLink()
    l.inject("$SWEEP 1 1 1\n", "noise\n")
    assert l.poll() == ["$SWEEP 1 1 1\n", "noise\n"] and l.poll() == []
    l.send("$STATUS\n"); assert l.sent == ["$STATUS\n"]


def test_mesh_packet_round_trip_and_rejects():
    nid = mesh.fnv1a16("dev-node-A")
    emb = np.arange(-8, 8, dtype=np.int8)
    pkt = mesh.encode_packet(nid, emb)
    assert len(pkt) == 20 and pkt[0] == mesh.MESH_MAGIC
    got = mesh.decode_packet(pkt)
    assert got[0] == nid and (got[1] == emb).all()
    assert mesh.decode_packet(pkt[:-1]) is None and mesh.decode_packet(b"PR" + pkt[2:]) is None
    assert mesh.fnv1a16("a") != mesh.fnv1a16("b")
    q = mesh.quantise(np.array([0.0, 1.0, -1.0, 100.0] + [0] * 12), 16.0)
    assert q.dtype == np.int8 and q[3] == 127 and q[1] == 16


def test_graph_inputs_shape_and_isolation():
    P, own = 3, np.random.default_rng(0).normal(size=(3, 16)).astype(np.float32)
    nbrs = [mesh.NeighbourHistory(i, np.ones((P, 16), np.float32) * i, w, 0) for i, w in ((1, 0.9), (2, 0.3), (3, 0.6))]
    hist, adj, valid = mesh.build_graph_inputs(own, nbrs, max_neighbours=2)
    assert hist.shape == (1, 3, 16, P) and adj.shape == (1, 3, 3) and valid.tolist() == [[1, 1, 1]]
    assert adj[0, 0, 1] == 0.9 and adj[0, 0, 2] == 0.6          # strongest two kept, weakest dropped
    assert (hist[0, 0] == own.T).all()
    assert mesh.adjacency_prior((0, 0), (3000, 0)) == pytest.approx(np.exp(-1)) and mesh.adjacency_prior(None, (1, 1)) == 0.5
