"""Model wiring: shapes, head routing, graph-stage skip, dense≡sparse A3TGCN."""
import numpy as np
import pytest
import torch

from prahari_train import heads as H
from prahari_train import model as M


@pytest.fixture(scope="module")
def net():
    torch.manual_seed(0)
    return M.PrahariModel(pretrained=False).eval()   # random-init TTM: no network in tests


def test_heads_spec_matches_blueprint_s9():
    assert H.GRAPH_HEADS == ("FL", "UF", "FI", "PO")
    assert H.DECLARED_ONLY_HEADS == ("GL", "WQ")
    assert H.N_OUTPUTS == 15
    assert H.BY_CODE["HW"].horizons_h == (72,) and H.BY_CODE["CY"].horizons_h == (48,)
    assert H.BY_CODE["UF"].horizons_h == (0, 6)


def test_standalone_forward_shapes(net):
    x = torch.randn(2, M.CONTEXT_LENGTH, 41)
    m = (torch.rand(2, M.CONTEXT_LENGTH, 41) > 0.1).float()
    out = net(x, m)
    assert out["emb"].shape == (2, 16) and out["logits"].shape == (2, 15) and out["probs"].shape == (2, 15)
    assert ((out["probs"] > 0) & (out["probs"] < 1)).all()
    pc = net.parameter_counts()
    assert 5e5 < pc["encoder_backbone"] < 1.2e6 and pc["temperature"] == 9


def test_masked_values_do_not_leak(net):
    """A masked channel's value must not change the embedding (value×mask = 0)."""
    x = torch.randn(1, M.CONTEXT_LENGTH, 41)
    m = torch.ones(1, M.CONTEXT_LENGTH, 41); m[:, :, 5] = 0
    x2 = x.clone(); x2[:, :, 5] = 1e6
    with torch.no_grad():
        assert torch.allclose(net.embed(x, m), net.embed(x2, m), atol=1e-6)


def test_graph_heads_read_refined_embedding_others_do_not(net):
    emb = torch.randn(3, 16); ref = emb + torch.randn(3, 16)
    with torch.no_grad():
        a = net.logits(emb); b = net.logits(emb, ref)
    sl = H.output_slices()
    for h in H.HEADS:
        same = torch.allclose(a[:, sl[h.code]], b[:, sl[h.code]])
        assert same != h.graph, h.code


def test_untrained_graph_stage_is_identity(net):
    hist = torch.randn(4, 16, M.GRAPH_PERIODS)
    adj = torch.tensor([[0, .5, .3, 0], [.5, 0, 0, .2], [.3, 0, 0, 0], [0, .2, 0, 0]])
    X, ei, ew, roots = M.build_batch_graph([(hist, adj, None)])
    with torch.no_grad():
        assert torch.allclose(net.graph(X, ei, ew), torch.zeros(4, 16))


def test_dense_graph_stage_matches_a3tgcn():
    torch.manual_seed(1)
    gs = M.GraphStage(periods=3)
    with torch.no_grad():                      # give the output layer real weights
        gs.out.weight.normal_(); gs.out.bias.normal_()
    dense = M.DenseGraphStage(gs).eval(); gs.eval()
    B, N = 2, 1 + M.MAX_NEIGHBOURS
    hist = torch.randn(B, N, 16, 3)
    adj = torch.rand(B, N, N); adj = (adj + adj.transpose(1, 2)) / 2; adj = adj * (adj > 0.5)
    for b in range(B):
        adj[b].fill_diagonal_(0)
    valid = torch.ones(B, N); valid[0, 5:] = 0; valid[1, 3:] = 0
    with torch.no_grad():
        d = dense(hist, adj, valid)
        # sparse reference: only valid nodes, edges among them
        samples = []
        for b in range(B):
            n = int(valid[b].sum())
            samples.append((hist[b, :n], adj[b, :n, :n], None))
        X, ei, ew, roots = M.build_batch_graph(samples)
        s = gs(X, ei, ew)[roots]
    assert torch.allclose(d, s, atol=1e-5), (d - s).abs().max()


def test_temperature_scaling_and_quantisation(net):
    lg = torch.tensor([[2.0] * 15])
    with torch.no_grad():
        net.log_temp.fill_(0.0); p1 = net.calibrated_probs(lg)
        net.log_temp.fill_(np.log(2.0)); p2 = net.calibrated_probs(lg)
        net.log_temp.fill_(0.0)
    assert torch.allclose(p1, torch.sigmoid(torch.tensor(2.0)).expand_as(p1))
    assert torch.allclose(p2, torch.sigmoid(torch.tensor(1.0)).expand_as(p2))
    e = torch.tensor([[0.0, 1.0, -1.0, 100.0, 0.031, -0.031, 0.0, 0.0, 0, 0, 0, 0, 0, 0, 0, 0]])
    q = M.quantise_embedding(e)
    assert q.dtype == torch.int8 and q.shape == (1, 16) and q[0, 3] == 127 and q[0, 1] == 16
    assert torch.allclose(M.dequantise_embedding(q)[0, :3], e[0, :3])
