"""Fit only the HUNT linear output; preserve every competing intent logit.

An intent-head update can change ATTACK into HOLD without choosing HUNT.
This narrower correction keeps all shared features, adapters and other output
rows exact. It is supervised calibration on native prefixes, not PPO.
"""
import torch
import torch.nn.functional as F
from ranker_commander_model import HEAD_OFFSETS,HEAD_SIZES


def fit_hunt_output(policy,batch,positive,*,epochs=40,lr=.002,seed=20260911303,progress=None):
    positive=positive.bool();off,width=HEAD_OFFSETS[3],HEAD_SIZES[3]
    eligible=(batch['actions'][:,2]>0)&batch['masks'][:,off+5]
    assert positive.any() and not (positive&~eligible).any()
    passive_choice=torch.isin(batch['actions'][:,3],batch['actions'].new_tensor([0,1,5]))
    assert not (positive&~passive_choice).any(),'Do not teach HUNT over the incumbent attack/scout/retreat entry'
    before={n:p.detach().clone() for n,p in policy.named_parameters()}
    features=[];raw=[];adapter=[]
    with torch.no_grad():
        for start in range(0,len(positive),512):
            x={k:v[start:start+512] for k,v in batch.items()}
            trunk,_=policy._features(x['vector'],x['maps'],x.get('privileged'))
            f=torch.cat([trunk]+[policy.embeddings[h](x['actions'][:,h]) for h in range(3)],1)
            extra=policy.head_adapters['3'](f) if policy.has_adapters else f.new_zeros((len(f),width))
            features.append(f);raw.append(policy.heads[3](f)+extra);adapter.append(extra[:,5])
    features=torch.cat(features)[eligible];raw=torch.cat(raw)[eligible];adapter=torch.cat(adapter)[eligible]
    y=positive[eligible];mask=batch['masks'][eligible,off:off+width].clone();mask[:,5]=False
    protect=(~passive_choice[eligible])|(batch['vector'][eligible,46]>0)
    assert mask.any(1).all() and (~y).any()
    competitors=raw.masked_fill(~mask,-torch.inf);denominator=competitors.logsumexp(1);best=competitors.max(1).values
    # Warm only the HUNT linear row, without replacing any adapter features.
    weight=torch.nn.Parameter(policy.heads[3].weight[0].detach().clone())
    bias=torch.nn.Parameter(policy.heads[3].bias[0].detach().clone()-3.)
    with torch.no_grad():bias.add_(-1.-(features[y]@weight+bias+adapter[y]-denominator[y]).median())
    optimizer=torch.optim.Adam([weight,bias],lr=lr)
    generator=torch.Generator().manual_seed(seed);curve=[]
    for epoch in range(epochs):
        losses=[]
        for idx in torch.randperm(len(y),generator=generator).split(256):
            gap=features[idx]@weight+bias+adapter[idx]-denominator[idx]
            loss=F.binary_cross_entropy_with_logits(gap,y[idx].float(),pos_weight=gap.new_tensor(2.))
            optimizer.zero_grad(set_to_none=True);loss.backward();torch.nn.utils.clip_grad_norm_([weight,bias],1.,error_if_nonfinite=True);optimizer.step();losses.append(float(loss.detach()))
        item=dict(epoch=epoch+1,loss=sum(losses)/len(losses));curve.append(item)
        if progress and (epoch+1)%10==0:progress(item)
    with torch.no_grad():
        margin=features@weight+bias+adapter-best
        # Bound false HUNT choices on recorded negative examples. This is a
        # training-set calibration, not a safety guarantee on future games.
        adjustment=max(0.,float(torch.quantile(margin[~y],.995))+.05)
        if protect.any():adjustment=max(adjustment,float(margin[protect].max())+.05)
        bias.sub_(adjustment)
        policy.heads[3].weight[5].copy_(weight);policy.heads[3].bias[5].copy_(bias)
        chosen=features@weight+bias+adapter>=best
        assert not chosen[protect].any()
    for name,p in policy.named_parameters():
        if name=='heads.3.weight':assert torch.equal(p.detach()[[0,1,2,3,4,6,7]],before[name][[0,1,2,3,4,6,7]])
        elif name=='heads.3.bias':assert torch.equal(p.detach()[[0,1,2,3,4,6,7]],before[name][[0,1,2,3,4,6,7]])
        else:assert torch.equal(p,before[name]),name
    return dict(method='HUNT linear row only, supervised BCE with fixed competing logits',epochs=epochs,lr=lr,seed=seed,
        native_eligible_prefixes=len(y),positive_labels=int(y.sum()),positive_chosen=int((chosen&y).sum()),
        negative_labels=int((~y).sum()),negative_hunt_choices=int((chosen&~y).sum()),bias_calibration=adjustment,
        protected_attack_scout_retreat_or_threat_rows=int(protect.sum()),protected_rows_hunt_choices=int(chosen[protect].sum()),
        all_other_parameters_exact=True,all_nonhunt_intent_outputs_preserved=True,curve=curve,
        limitation='Native-prefix fit diagnostics; actual behavior and wins require fresh native evaluation.')
