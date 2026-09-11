"""Native-mask and danger boundaries for head-local behavioral supervision."""
import unittest
import torch
from ranker_commander_model import HEAD_OFFSETS, HEAD_SIZES, VECTOR_SIZE
from ranker_commander_behavior_prior import behavior_targets, behavior_loss, bootstrap_hunt, fit_behavior


class BehaviorPriorTests(unittest.TestCase):
    def test_behavior_fit_preserves_economy_critic_and_native_action_records(self):
        from ranker_commander_model import CommanderPolicy,MAP_SHAPE
        from ranker_commander_combat_ppo import freeze_economy
        torch.set_num_threads(2)
        torch.manual_seed(818)
        p=CommanderPolicy()
        freeze_economy(p)
        batch,_=self.fixture(8)
        batch.update(maps=torch.zeros(8,*MAP_SHAPE),privileged=torch.zeros(8,32),target=torch.zeros(8))
        with torch.no_grad():ref=p.evaluate(batch['vector'],batch['maps'],batch['actions'],batch['masks'],batch['privileged'])['logits']
        labels=behavior_targets(batch,ref)
        bootstrap_hunt(p)
        original_actions=batch['actions'].clone()
        before={n:v.detach().clone() for n,v in p.named_parameters()}
        flags={n:v.requires_grad for n,v in p.named_parameters()}
        with torch.no_grad():start=behavior_loss(p.evaluate(batch['vector'],batch['maps'],batch['actions'],batch['masks'],batch['privileged'])['logits'],batch['masks'],labels['targets'],labels['selected'])
        _,report=fit_behavior(p,batch,labels,ref,epochs=2,minibatch=4,lr=1e-3)
        self.assertTrue(report['changed_parameters'])
        with torch.no_grad():end=behavior_loss(p.evaluate(batch['vector'],batch['maps'],batch['actions'],batch['masks'],batch['privileged'])['logits'],batch['masks'],labels['targets'],labels['selected'])
        self.assertLess(end,start)
        for n,v in p.named_parameters():
            self.assertEqual(v.requires_grad,flags[n])
            if not n.startswith(tuple(prefix for h in (2,3,4) for prefix in (f'heads.{h}.',f'head_adapters.{h}.'))):
                self.assertTrue(torch.equal(v,before[n]),n)
        self.assertTrue(torch.equal(original_actions,batch['actions']))

    def test_hunt_bootstrap_changes_only_its_intent_logit(self):
        from ranker_commander_model import CommanderPolicy,MAP_SHAPE
        torch.set_num_threads(2)
        torch.manual_seed(814)
        p=CommanderPolicy()
        v=torch.rand(3,VECTOR_SIZE)
        maps=torch.rand(3,*MAP_SHAPE)
        actions=torch.zeros(3,8,dtype=torch.long)
        masks=torch.ones(3,sum(HEAD_SIZES),dtype=torch.bool)
        with torch.no_grad():before=p.evaluate(v,maps,actions,masks)['logits']
        bootstrap_hunt(p)
        with torch.no_grad():after=p.evaluate(v,maps,actions,masks)['logits']
        offset=HEAD_OFFSETS[3]
        self.assertTrue(torch.allclose(after[:,offset+5],after[:,offset]-4,atol=1e-6))
        keep=[i for i in range(sum(HEAD_SIZES)) if i!=offset+5]
        self.assertTrue(torch.equal(before[:,keep],after[:,keep]))

    def fixture(self,n=6):
        v=torch.zeros(n,VECTOR_SIZE)
        v[:,564]=torch.log1p(torch.tensor(8.))/torch.log1p(torch.tensor(180.))
        v[:,565]=torch.log1p(torch.tensor(2000.))/torch.log1p(torch.tensor(20000.))
        v[:,172]=1
        v[:,30]=.2
        v[:,317]=1
        v[:,318:320]=.3
        v[:,230:232]=.3
        v[:,323]=.05
        a=torch.zeros(n,8,dtype=torch.long)
        a[:,2]=1
        masks=torch.ones(n,sum(HEAD_SIZES),dtype=torch.bool)
        logits=torch.zeros(n,sum(HEAD_SIZES))
        return dict(vector=v,actions=a,masks=masks),logits

    def test_hunt_requires_native_legal_main_safe_idle_and_nearby_target(self):
        b,reference=self.fixture()
        b['vector'][1,46]=.1
        b['masks'][2,HEAD_OFFSETS[3]+5]=False
        b['actions'][3,2]=0
        b['vector'][4,323]=.5
        b['vector'][5,186]=1
        labels=behavior_targets(b,reference)
        self.assertEqual(labels['counts']['hunt'],1)
        self.assertEqual(labels['targets'][0,3].item(),5)
        self.assertEqual(labels['targets'][0,2].item(),1)
        self.assertEqual(labels['selected'][:,3].tolist(),[True,False,False,False,False,False])
        # Head 4 retains the ACTUAL prefix mask. Hunt's forced anchor is
        # resolved by the native engine after it chooses intent 5.
        self.assertFalse(labels['selected'][:,4].any())

    def test_attack_continuity_releases_for_retreat_danger_stale_or_invalid_goal(self):
        b,reference=self.fixture()
        v=b['vector']
        v[:,178]=2/8
        v[:,179]=6/16
        v[:,229+8*6]=1
        v[:,576]=torch.log1p(torch.tensor(64.))/torch.log1p(torch.tensor(60000.))
        v[1,46]=.1
        v[2,186]=1
        v[3,229+8*6]=0
        v[4,576]=torch.log1p(torch.tensor(512.))/torch.log1p(torch.tensor(60000.))
        v[5,577]=torch.log1p(torch.tensor(4000.))/torch.log1p(torch.tensor(20000.))
        labels=behavior_targets(b,reference)
        self.assertEqual(labels['counts']['continuity'],1)
        self.assertEqual(labels['targets'][0,2].item(),0)
        self.assertEqual(labels['selected'][:,2].tolist(),[True,False,False,False,False,False])

    def test_losses_touch_only_selected_legal_heads_and_leave_native_actions_intact(self):
        b,reference=self.fixture(1)
        original=b['actions'].clone()
        labels=behavior_targets(b,reference)
        logits=torch.zeros_like(reference,requires_grad=True)
        loss=behavior_loss(logits,b['masks'],labels['targets'],labels['selected'])
        loss.backward()
        self.assertTrue(torch.equal(b['actions'],original))
        self.assertLess(logits.grad[0,HEAD_OFFSETS[3]+5],0)
        self.assertLess(logits.grad[0,HEAD_OFFSETS[2]+1],0)
        for h in (0,1,4,5,6,7):
            self.assertEqual(float(logits.grad[:,HEAD_OFFSETS[h]:HEAD_OFFSETS[h]+HEAD_SIZES[h]].abs().sum()),0)


if __name__=='__main__':unittest.main()
