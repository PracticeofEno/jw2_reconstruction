"""Refine HUNT entry and continuation using only native head-3 prefixes."""
import torch
import torch.nn.functional as F
from ranker_commander_model import HEAD_OFFSETS


def fit_hunt_continuity(policy,batch,positive,*,epochs=160,lr=.002,seed=20260911523):
    before={n:p.detach().clone() for n,p in policy.named_parameters()}
    off=HEAD_OFFSETS[3];eligible=(batch['actions'][:,2]>0)&batch['masks'][:,off+5]
    positive=positive.bool();assert positive.any() and not (positive&~eligible).any()
    features=[];raw=[];adapter=policy.head_adapters['3']
    with torch.no_grad():
        for start in range(0,len(positive),512):
            x={k:v[start:start+512] for k,v in batch.items()}
            trunk,_=policy._features(x['vector'],x['maps'],x['privileged'])
            f=torch.cat([trunk]+[policy.embeddings[h](x['actions'][:,h]) for h in range(3)],1)
            hidden=adapter.down(f).relu();features.append(torch.cat((f,hidden),1));raw.append(policy.heads[3](f)+adapter.up(hidden))
        features=torch.cat(features)[eligible];raw=torch.cat(raw)[eligible];y=positive[eligible]
        legal=batch['masks'][eligible,off:off+8].clone();baseline=raw.masked_fill(~legal,-torch.inf).softmax(1)[:,5]
        legal[:,5]=False;competitors=raw.masked_fill(~legal,-torch.inf);best=competitors.max(1).values;denom=competitors.logsumexp(1)
        target=baseline.clone();target[y]=torch.maximum(target[y],target.new_full((int(y.sum()),),.9))
        intent=batch['actions'][eligible,3]
        protected=((intent>=2)&(intent!=5)|(batch['vector'][eligible,46]>0))&~y
        weight=torch.nn.Parameter(torch.cat((policy.heads[3].weight[5],adapter.up.weight[5])).clone())
        bias=torch.nn.Parameter((policy.heads[3].bias[5]+adapter.up.bias[5]).clone())
    opt=torch.optim.Adam([weight,bias],lr=lr);g=torch.Generator().manual_seed(seed);curve=[]
    for epoch in range(epochs):
        losses=[]
        for idx in torch.randperm(len(y),generator=g).split(256):
            logits=features[idx]@weight+bias
            loss=(F.binary_cross_entropy_with_logits(logits-denom[idx],target[idx],reduction='none')*torch.where(y[idx],4.,1.)).mean()
            if protected[idx].any():loss=loss+4*F.relu(logits[protected[idx]]-best[idx][protected[idx]]+.3).mean()
            opt.zero_grad(set_to_none=True);loss.backward();torch.nn.utils.clip_grad_norm_([weight,bias],1.,error_if_nonfinite=True);opt.step();losses.append(float(loss.detach()))
        if (epoch+1)%40==0:curve.append(dict(epoch=epoch+1,loss=sum(losses)/len(losses)))
    with torch.no_grad():
        margin=features@weight+bias-best
        adjust=max(0.,float(margin[protected].max())+.02) if protected.any() else 0.;bias.sub_(adjust)
        n=policy.heads[3].weight.shape[1]
        policy.heads[3].weight[5].copy_(weight[:n]);adapter.up.weight[5].copy_(weight[n:]);policy.heads[3].bias[5].copy_(bias);adapter.up.bias[5].zero_()
        chosen=features@weight+bias>=best
        continuation=y&(torch.round(batch['vector'][eligible,178]*8)==5)
        entry=y&~continuation
    for name,p in policy.named_parameters():
        if name in ['heads.3.weight','heads.3.bias','head_adapters.3.up.weight','head_adapters.3.up.bias']:
            assert torch.equal(p[[0,1,2,3,4,6,7]],before[name][[0,1,2,3,4,6,7]]),name
        else:assert torch.equal(p,before[name]),name
    assert not chosen[protected].any()
    return dict(method='Supervised HUNT row and adapter output row; native prefixes, no PPO',epochs=epochs,lr=lr,seed=seed,
        positive_labels=int(y.sum()),positive_chosen=int(chosen[y].sum()),entry_labels=int(entry.sum()),entry_chosen=int(chosen[entry].sum()),
        continuation_labels=int(continuation.sum()),continuation_chosen=int(chosen[continuation].sum()),
        protected_rows=int(protected.sum()),protected_changes=int(chosen[protected].sum()),calibration=adjust,
        all_other_parameters_exact=True,curve=curve)
