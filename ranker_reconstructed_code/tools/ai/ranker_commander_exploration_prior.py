"""Sparse hunting and attack-continuity labels on exact native head prefixes.

This is supervised correction, not relabeled PPO experience. Native actions,
likelihoods, masks and rewards are never overwritten. Reconnaissance itself is
an explicit executor helper; these targets make no claim of learning SCOUT.
"""
import torch
from ranker_commander_model import HEAD_OFFSETS, HEAD_SIZES


def exploration_targets(batch):
    v,a,m=batch['vector'],batch['actions'],batch['masks']
    def count(index,scale):
        return torch.expm1(v[:,index]*v.new_tensor(scale).log1p())
    n=count(564,180);own=count(565,20000);spread=count(567,4096)
    local_enemy=count(577,20000);visible_enemy=v[:,35]*20000
    intent=(v[:,178]*8).round().long();anchor=(v[:,179]*16).round().long().clamp(0,15)
    age=count(576,60000)
    safe=(v[:,46]==0)&(v[:,186]<.5)&(v[:,172]>=.75)&(v[:,573]<=.15)
    valid=v.gather(1,(229+anchor*8)[:,None]).squeeze(1)>.5
    changing_members=(a[:,0]>=38)&(a[:,0]<=41)
    # Preserve a recent offensive mission only when the compact force is
    # healthy, locally superior and still travelling to a valid objective.
    # Explicitly withhold this label under danger; do not ban retreat.
    advantage=(own>=1.35*torch.maximum(local_enemy,visible_enemy))&(own>=800)&(spread<=384)
    continuity=safe&advantage&valid&((intent==2)|(intent==3))
    continuity &= torch.isin(anchor,anchor.new_tensor([4,6,9,12]))&(age<=1024)&(v[:,185]*4096>256)&(n>=6)&~changing_members
    # The intent head is taught only where the native prefix actually selected
    # MAIN and its conditional mask permits HUNT. Do not invent a prefix mask.
    hunt=(a[:,2]==1)&m[:,HEAD_OFFSETS[3]+5]&safe&~changing_members
    hunt &= torch.isin(intent,intent.new_tensor([0,1,5]))&(n>=4)&(n<=24)&(v[:,172]>=.85)&(local_enemy==0)
    hunt &= (v[:,30]<.5)&(v[:,317]>.5)&(v[:,324]>=.99)&(v[:,323]*4096<=960)
    base_distance=((v[:,318:320]-v[:,230:232])*4096).square().sum(1).sqrt()
    hunt &= base_distance<=1280
    targets=a.clone();selected=torch.zeros_like(a,dtype=torch.bool)
    selected[:,2]=continuity|hunt;targets[continuity,2]=0;targets[hunt,2]=1
    selected[:,3]=hunt;targets[hunt,3]=5
    for head,width in enumerate(HEAD_SIZES):
        off=HEAD_OFFSETS[head]
        assert m[:,off:off+width].gather(1,targets[:,head,None])[selected[:,head]].all()
    return dict(targets=targets,selected=selected,counts=dict(hunt=int(hunt.sum()),continuity=int(continuity.sum()),attack_entry=0,enemy_building=0))
