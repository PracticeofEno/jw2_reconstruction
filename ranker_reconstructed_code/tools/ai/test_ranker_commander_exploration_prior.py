import unittest
import torch
from ranker_commander_model import HEAD_OFFSETS,HEAD_SIZES,VECTOR_SIZE
from ranker_commander_exploration_prior import exploration_targets

class ExplorationPriorTests(unittest.TestCase):
    def fixture(self,n):
        v=torch.zeros(n,VECTOR_SIZE);a=torch.zeros(n,8,dtype=torch.long);a[:,2]=1
        v[:,564]=torch.log1p(torch.tensor(10.))/torch.log1p(torch.tensor(180.))
        v[:,565]=torch.log1p(torch.tensor(3000.))/torch.log1p(torch.tensor(20000.))
        v[:,172]=1;v[:,317]=v[:,324]=1;v[:,30]=.2
        v[:,318:320]=v[:,230:232]=.1;v[:,323]=.1
        return dict(vector=v,actions=a,masks=torch.ones(n,sum(HEAD_SIZES),dtype=torch.bool))
    def test_hunt_exact_prefix_visibility_and_danger(self):
        b=self.fixture(6);v=b['vector'];a=b['actions'];original=a.clone()
        a[1,2]=0;b['masks'][2,HEAD_OFFSETS[3]+5]=False
        v[3,46]=.1;v[4,324]=.5;v[5,178]=2/8
        original=a.clone();r=exploration_targets(b)
        self.assertEqual(r['counts']['hunt'],1)
        self.assertEqual(r['selected'][:,3].tolist(),[True,False,False,False,False,False])
        self.assertTrue(torch.equal(a,original));self.assertFalse(r['selected'][:,4].any())
    def test_advantage_does_not_suppress_necessary_retreat_or_completed_mission(self):
        b=self.fixture(7);v=b['vector'];v[:,178]=2/8;v[:,179]=6/16;v[:,277]=1;v[:,185]=.3
        v[:,576]=torch.log1p(torch.tensor(700.))/torch.log1p(torch.tensor(60000.))
        v[1,35]=.3;v[2,46]=.1;v[3,172]=.5;v[4,186]=1;v[5,185]=.01
        v[6,567]=torch.log1p(torch.tensor(600.))/torch.log1p(torch.tensor(4096.))
        r=exploration_targets(b)
        self.assertEqual(r['counts']['continuity'],1);self.assertEqual(r['targets'][0,2],0)
        self.assertEqual(r['selected'][:,2].tolist(),[True,False,False,False,False,False,False])

if __name__=='__main__':unittest.main()
