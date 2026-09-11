"""Supervise existing MAIN/HUNT output rows on native counterfactual labels.

The actor's RLO actions/logp are never edited or used as synthetic PPO samples.
The caller supplies masks recorded by -AIHUNTLABELS under each label's actual
autoregressive prefix. All competing output rows and shared features stay exact.
"""
import torch
import torch.nn.functional as F
from ranker_commander_model import HEAD_OFFSETS, HEAD_SIZES


def fit_idle_hunt(policy, batch, positive, *, epochs=80, lr=.003, seed=20260911):
    positive=positive.bool()
    assert positive.any()
    assert ((batch['actions'][positive,2]==1)&(batch['actions'][positive,3]==5)&(batch['actions'][positive,4]==11)).all()
    assert (batch['actions'][:,0:2]==batch['actor_actions'][:,0:2]).all()
    for h in range(8):
        off=HEAD_OFFSETS[h]
        assert batch['masks'].gather(1,(off+batch['actions'][:,h])[:,None]).all()
    before={n:p.detach().clone() for n,p in policy.named_parameters()}
    trunk=[]
    with torch.no_grad():
        for first in range(0,len(positive),512):
            last=first+512
            t,_=policy._features(batch['vector'][first:last],batch['maps'][first:last],batch['privileged'][first:last]);trunk.append(t)
    trunk=torch.cat(trunk);reports={}
    for head,output,warm in [(3,5,0),(2,1,1)]:
        off,width=HEAD_OFFSETS[head],HEAD_SIZES[head]
        masks=batch['masks'][:,off:off+width].clone()
        eligible=masks[:,output]&(masks.sum(1)>1)
        if head==3:eligible&=batch['actions'][:,2]>0
        assert not (positive&~eligible).any()
        y=positive[eligible];actor=batch['actor_actions'][eligible]
        with torch.no_grad():
            x=torch.cat([trunk]+[policy.embeddings[h](batch['actions'][:,h]) for h in range(head)],1)[eligible]
            adapter=policy.head_adapters[str(head)]
            hidden=adapter.down(x).relu()
            features=torch.cat((x,hidden),1)
            raw=policy.heads[head](x)+adapter.up(hidden)
            legal=masks[eligible];original_prob=raw.masked_fill(~legal,-torch.inf).softmax(1)[:,output]
            legal[:,output]=False
            competitors=raw.masked_fill(~legal,-torch.inf)
            denominator=competitors.logsumexp(1);best=competitors.max(1).values
            original_chosen=raw[:,output]>=best
            weight=torch.nn.Parameter(torch.cat((policy.heads[head].weight[warm],adapter.up.weight[warm])).clone())
            bias=torch.nn.Parameter((policy.heads[head].bias[warm]+adapter.up.bias[warm]-(4. if head==3 else 0.)).clone())
            target=original_prob.clone();target[y]=torch.maximum(target[y],target.new_full((int(y.sum()),),.85))
            # Explicitly protect recorded attack/scout/retreat decisions that
            # did not already choose this row; this is a finite-data check.
            protected=(actor[:,2]>0)&(actor[:,3]>=2)&(actor[:,3]!=5)&~original_chosen&~y
        optimizer=torch.optim.Adam([weight,bias],lr=lr)
        gen=torch.Generator().manual_seed(seed+head);curve=[]
        for epoch in range(epochs):
            loss_sum=0.;steps=0
            for idx in torch.randperm(len(y),generator=gen).split(256):
                logit=features[idx]@weight+bias
                loss=(F.binary_cross_entropy_with_logits(logit-denominator[idx],target[idx],reduction='none')*torch.where(y[idx],3.,1.)).mean()
                if protected[idx].any():loss=loss+2*F.relu(logit[protected[idx]]-best[idx][protected[idx]]+.1).mean()
                optimizer.zero_grad(set_to_none=True);loss.backward();torch.nn.utils.clip_grad_norm_([weight,bias],1.,error_if_nonfinite=True);optimizer.step()
                loss_sum+=float(loss.detach());steps+=1
            if (epoch+1)%20==0:curve.append(dict(epoch=epoch+1,loss=loss_sum/steps))
        with torch.no_grad():
            margin=features@weight+bias-best
            correction=max(0.,float(margin[protected].max())+.02) if protected.any() else 0.
            bias.sub_(correction)
            # Store a single intercept. Competing adapter and linear rows are unchanged.
            policy.heads[head].weight[output].copy_(weight[:x.shape[1]])
            adapter.up.weight[output].copy_(weight[x.shape[1]:])
            policy.heads[head].bias[output].copy_(bias);adapter.up.bias[output].zero_()
            new_raw=features@weight+bias
            chosen=new_raw>=best;prob=(new_raw-denominator).sigmoid()
            assert not chosen[protected].any()
        reports[str(head)]=dict(output=output,eligible=len(y),positives=int(y.sum()),
            positive_choices=int(chosen[y].sum()),positive_probability_before=float(original_prob[y].mean()),
            positive_probability_after=float(prob[y].mean()),new_negative_choices=int((chosen&~original_chosen&~y).sum()),
            protected=int(protected.sum()),protected_changes=int(chosen[protected].sum()),calibration=correction,curve=curve)
    for name,p in policy.named_parameters():
        rows=None
        for head,out in [(2,1),(3,5)]:
            if name in [f'heads.{head}.weight',f'heads.{head}.bias',f'head_adapters.{head}.up.weight',f'head_adapters.{head}.up.bias']:
                rows=[i for i in range(HEAD_SIZES[head]) if i!=out]
        if rows is None:assert torch.equal(p,before[name]),name
        else:assert torch.equal(p[rows],before[name][rows]),name
    return dict(method='Supervised native idle HUNT labels; MAIN and HUNT output rows only',
                ppo=False,epochs=epochs,lr=lr,seed=seed,other_parameters_exact=True,heads=reports)
