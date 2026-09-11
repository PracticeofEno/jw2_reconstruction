"""Head-local behavior supervision from native-legal training observations.

Targets supplement PPO; recorded actions, likelihoods and rewards stay intact.
Hunt is taught only on native MAIN prefixes where the intent is legal. Its
anchor is forced to 11 by the existing native conditional mask, so no invented
counterfactual anchor mask or synthetic PPO action is needed.
"""
import torch
from ranker_commander_model import HEAD_OFFSETS, HEAD_SIZES


def bootstrap_hunt(policy, gap=4.):
    """Recover exploration of an output suppressed by zero hunt demonstrations.

    Copy HOLD's full logit row to HUNT with a negative offset. Every other
    conditional intent logit remains unchanged; this is training initialization,
    followed by supervised correction and native evaluation, not a runtime rule.
    """
    with torch.no_grad():
        policy.heads[3].weight[5].copy_(policy.heads[3].weight[0])
        policy.heads[3].bias[5].copy_(policy.heads[3].bias[0]-gap)
        adapter=policy.head_adapters['3'].up
        adapter.weight[5].copy_(adapter.weight[0])
        adapter.bias[5].copy_(adapter.bias[0])


def behavior_targets(batch, reference_logits):
    v, a, masks = batch['vector'], batch['actions'], batch['masks']
    def count(index,scale):
        return torch.expm1(v[:,index] * v.new_tensor(scale).log1p())
    def choice(head):
        offset,width=HEAD_OFFSETS[head],HEAD_SIZES[head]
        return reference_logits[:,offset:offset+width].detach().masked_fill(~masks[:,offset:offset+width],-torch.inf).argmax(1)
    intent=(v[:,178]*8).round().long()
    anchor=(v[:,179]*16).round().long().clamp(0,15)
    anchor_valid=v.gather(1,(229+8*anchor)[:,None]).squeeze(1)>.5
    age=count(576,60000)
    main_count=count(564,180)
    main_weight=count(565,20000)
    enemy_weight=count(577,20000)
    safe=(v[:,46]==0)&(v[:,186]<.5)&(v[:,172]>=.65)&(enemy_weight<=.8*main_weight)
    continuity=safe&((intent==2)|(intent==3))&((anchor==4)|(anchor==6)|(anchor==9)|(anchor==12))&anchor_valid&(age<=256)
    continuity &= ~((a[:,0]>=38)&(a[:,0]<=41))
    hunt=(a[:,2]==1)&masks[:,HEAD_OFFSETS[3]+5]&(intent==0)&(main_count>=4)&(main_count<=12)
    hunt &= safe&(enemy_weight==0)&(v[:,172]>=.8)&(v[:,30]<.4)&(v[:,317]>.5)&(v[:,323]*4096<=640)
    base_distance=((v[:,318:320]-v[:,230:232])*4096).square().sum(1).sqrt()
    hunt &= base_distance<=640
    entry=(a[:,2]==1)&(choice(2)==1)&((choice(3)==2)|(choice(3)==3))&((intent==0)|(intent==1)|(intent==6))
    entry &= ~hunt
    building=(a[:,2]==1)&((a[:,3]==2)|(a[:,3]==3))&(choice(4)==6)&masks[:,HEAD_OFFSETS[4]+6]
    targets=a.clone()
    selected=torch.zeros_like(a,dtype=torch.bool)
    selected[:,2]=continuity|entry|hunt
    targets[continuity,2]=0
    targets[entry|hunt,2]=1
    selected[:,3]=hunt
    targets[hunt,3]=5
    selected[:,4]=building
    targets[building,4]=6
    # Validate only the changed head under its exact recorded native prefix.
    for head,width in enumerate(HEAD_SIZES):
        offset=HEAD_OFFSETS[head]
        assert masks[:,offset:offset+width].gather(1,targets[:,head,None])[selected[:,head]].all()
    return dict(targets=targets,selected=selected,counts=dict(hunt=int(hunt.sum()),continuity=int(continuity.sum()),
        attack_entry=int(entry.sum()),enemy_building=int(building.sum())))


def behavior_loss(logits, masks, targets, selected):
    losses=[]
    for head in (2,3,4):
        rows=selected[:,head]
        if not rows.any():continue
        offset,width=HEAD_OFFSETS[head],HEAD_SIZES[head]
        masked=logits[rows,offset:offset+width].masked_fill(~masks[rows,offset:offset+width],-torch.inf)
        losses.append(torch.nn.functional.cross_entropy(masked,targets[rows,head]))
    return torch.stack(losses).mean() if losses else logits.sum()*0


def fit_behavior(policy,batch,labels,anchor_logits,*,epochs=12,minibatch=128,lr=1e-4,seed=20260910,progress_callback=None):
    """Sparse head-local supervision plus KL on uniform training-state replay."""
    original={n:p.requires_grad for n,p in policy.named_parameters()}
    before={n:p.detach().clone() for n,p in policy.named_parameters()}
    prefixes=tuple(prefix for h in (2,3,4) for prefix in (f'heads.{h}.',f'head_adapters.{h}.'))
    for name,p in policy.named_parameters():
        p.requires_grad_(name.startswith(prefixes))
        p.grad=None
    indices=torch.where(labels['selected'].any(1))[0]
    assert len(indices)>0 and labels['counts']['hunt']>0
    optimizer=torch.optim.Adam(policy.parameters(),lr=lr)
    generator=torch.Generator().manual_seed(seed)
    reports=[]
    try:
        for epoch in range(epochs):
            epoch_rows=[]
            order=indices[torch.randperm(len(indices),generator=generator)]
            for chosen in order.split(minibatch):
                replay=torch.randint(len(batch['target']),(len(chosen),),generator=generator)
                union=torch.cat((chosen,replay))
                x={k:v[union] for k,v in batch.items()}
                output=policy.evaluate(x['vector'],x['maps'],x['actions'],x['masks'],x['privileged'])
                logits=output['logits']
                correction=behavior_loss(logits[:len(chosen)],batch['masks'][chosen],labels['targets'][chosen],labels['selected'][chosen])
                kl=logits.new_zeros(())
                for head in (2,3,4):
                    off,width=HEAD_OFFSETS[head],HEAD_SIZES[head]
                    legal=batch['masks'][replay,off:off+width]
                    current=logits[len(chosen):,off:off+width].masked_fill(~legal,-1e9).log_softmax(1)
                    old=anchor_logits[replay,off:off+width].detach().masked_fill(~legal,-1e9).log_softmax(1)
                    kl=kl+(old.exp()*(old-current)).sum(1).mean()
                loss=correction+.5*kl
                assert torch.isfinite(loss)
                optimizer.zero_grad(set_to_none=True)
                loss.backward()
                torch.nn.utils.clip_grad_norm_(policy.parameters(),1.,error_if_nonfinite=True)
                optimizer.step()
                epoch_rows.append((float(correction.detach()),float(kl.detach())))
            report=dict(epoch=epoch+1,epochs=epochs,steps=len(epoch_rows),correction_loss=sum(x[0] for x in epoch_rows)/len(epoch_rows),
                replay_kl=sum(x[1] for x in epoch_rows)/len(epoch_rows))
            reports.append(report)
            if progress_callback:progress_callback(report)
        changed=[n for n,p in policy.named_parameters() if not torch.equal(p,before[n])]
        assert changed and all(n.startswith(prefixes) for n in changed)
        return optimizer,dict(epochs=epochs,minibatch=minibatch,lr=lr,replay_kl_coefficient=.5,
            corrections=labels['counts'],correction_rows=len(indices),reports=reports,changed_parameters=changed)
    finally:
        for name,p in policy.named_parameters():
            p.requires_grad_(original[name])
            p.grad=None
