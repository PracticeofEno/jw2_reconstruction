"""Label-only provenance/environment checks; subprocesses never run games."""
import copy
import os
from pathlib import Path
import unittest
from unittest.mock import patch
import numpy as np
import ranker_commander_eval as evaluation
import ranker_commander_rollout as rollout
import test_ranker_commander_strategy_router as fixture_module
from ranker_commander_strategy import sha


class DaggerLabelRouterTests(unittest.TestCase):
    def setUp(self):
        fixture=fixture_module.StrategyRouterTest('test_public_race_only_and_baseline_child_is_allowed')
        fixture.setUp();self.addCleanup(fixture.doCleanups);self.f=fixture
        self.job={**fixture.job(1), 'dagger':True,'dagger_label_router':fixture.binding,
            'dagger_label_strategy':fixture.children[1]}
        self.options=dict(teacher=False,weights2=None,executable_sha=sha(fixture.exe))

    def native_log(self,tribe=1):
        d=self.f.children[tribe]['definition']
        return '[ElfStrategyProfile] crc=%08x values=%s path=%s'%(d['profile_crc32'],
            ','.join(format(v,'.17g')for v in d['parameters']),d['profile_path'])

    def episode(self,context=True):
        records=np.zeros(3,dtype=rollout.RECORD_DTYPE)
        records['frame']=[1,33,60000];records['delta_frame']=[1,32,59967]
        records['weight_version']=20;records['status'][-1]=rollout.TRUNCATED
        records['vector'][:,607]=1;records['vector'][:,67]=1
        records['vector'][:,536:542]=[.25,0,1,0,0,0]if context else[.25,0,0,0,0,0]
        records['mask'][:2]=1
        path=self.f.directory/'actor.rlo'
        rollout.write_rollout(path,records,owner=1,seed=self.job['seed'],weight_version=20)
        ep=rollout.read_rollout(path,teacher=False);self.addCleanup(ep.close)
        labels=np.zeros(3,dtype=rollout.LABEL_RECORD)
        labels['mask_packed'][:2]=np.packbits(records['mask'][:2],axis=1,bitorder='little')
        labels['action'][0,0]=15
        self.write_labels(ep,labels)
        return ep,labels

    @staticmethod
    def write_labels(ep,labels):
        Path(str(ep.path)+'.teacher.bin').write_bytes(rollout.LABEL_MAGIC+labels.tobytes())

    def test_routing_uses_only_public_opponent_and_exact_frozen_child(self):
        definition=evaluation.validate_dagger_label_job(self.job,**self.options,log=self.native_log())
        self.assertEqual(definition,self.f.children[1]['definition'])
        changed={**self.job,'seed':812,'start_pair':[3,2]}
        self.assertEqual(evaluation.validate_dagger_label_job(changed,**self.options),definition)
        with self.assertRaisesRegex(ValueError,'public-opponent'):
            evaluation.validate_dagger_label_job({**self.job,'dagger_label_strategy':self.f.children[2]},**self.options)

    def test_rejects_wrong_actor_mode_context_runtime_and_mixed_controllers(self):
        invalid=[dict(dagger=False),dict(own_tribe=2),dict(curriculum=1),dict(max_frames=50000),
            dict(opp_slow=2),dict(coordinated_transfers=True),dict(teacher_variant=0),dict(teacher2=True),
            dict(primary_weights='override'),dict(controller={'overlay':True}),dict(elf_strategy_profile=self.f.children[1])]
        for changes in invalid:
            with self.subTest(changes=changes),self.assertRaises(ValueError):
                evaluation.validate_dagger_label_job({**self.job,**changes},**self.options)
        for options in ({**self.options,'teacher':True},{**self.options,'weights2':'other'},
                        {**self.options,'executable_sha':'bad'}):
            with self.subTest(options=options),self.assertRaises(ValueError):
                evaluation.validate_dagger_label_job(self.job,**options)
        incomplete=dict(self.job);del incomplete['dagger_label_strategy']
        with self.assertRaises(ValueError):evaluation.validate_dagger_label_job(incomplete,**self.options)

    def test_native_profile_receipt_and_numeric_files_must_match(self):
        for log in ('',self.native_log()+'\n'+self.native_log(),self.native_log(2)):
            with self.subTest(log=log),self.assertRaises(ValueError):
                evaluation.validate_dagger_label_job(self.job,**self.options,log=log)
        Path(self.f.children[1]['definition']['profile_path']).write_text('JWELFSTRAT1\n0 0 0 0 0 0\n')
        with self.assertRaises(ValueError):evaluation.validate_dagger_label_job(self.job,**self.options)

    def test_label_environment_is_per_process_and_never_turns_actor_into_teacher(self):
        install=self.f.directory/'install';install.mkdir()
        weights=self.f.directory/'weights.bin';weights.write_bytes(b'actor identity only; not loaded')
        ordinary={k:v for k,v in self.job.items()if not k.startswith('dagger_label_')}
        ordinary.update(dagger=False,teacher_variant=0)
        cases=[(self.job,False),(ordinary,False),({**ordinary,'elf_strategy_profile':self.f.children[1]},True)]
        original_environment=os.environ.copy()
        with patch.dict(os.environ,{evaluation.STRATEGY_ENV:'must-not-inherit'}):
            for index,(job,teacher)in enumerate(cases):
                with patch.object(evaluation.subprocess,'Popen',side_effect=OSError('synthetic launch disabled'))as launch:
                    report=evaluation._run_game(install,weights,self.f.directory/'jobs',index,job,3,
                        teacher=teacher,deterministic=True,timeout=1,executable=self.f.exe)
                command=launch.call_args.args[0];environment=launch.call_args.kwargs['env']
                self.assertEqual('-AITEACHER'in command,teacher)
                self.assertEqual('-AIDAGGER'in command,index==0)
                self.assertIn('-AIWEIGHTS:'+str(weights),command)
                if index in (0,2):self.assertEqual(environment[evaluation.STRATEGY_ENV],self.f.children[1]['definition']['profile_path'])
                else:self.assertNotIn(evaluation.STRATEGY_ENV,environment)
                if index==0:
                    self.assertTrue(report['neural_actor']);self.assertFalse(report['teacher'])
                    self.assertTrue(report['label_controller_only']);self.assertNotIn('elf_strategy_profile',report)
                self.assertEqual(os.environ[evaluation.STRATEGY_ENV],'must-not-inherit')
        self.assertEqual(dict(os.environ),original_environment)

    def test_label_legality_count_terminal_and_actor_bytes_remain_independent(self):
        ep,labels=self.episode();before=ep.path.read_bytes();recorded=ep.decisions['action'].copy()
        kwargs=dict(executable_sha=sha(self.f.exe),log=self.native_log(),episode=ep)
        result=evaluation.validate_dagger_label_output(self.job,**kwargs)
        self.assertEqual(result['decisions'],2);self.assertTrue(result['label_only'])
        self.assertTrue(np.array_equal(recorded,ep.decisions['action']))
        self.assertEqual(ep.path.read_bytes(),before)
        bad=labels.copy();bad['action'][-1,0]=1
        variants=[bad,labels[:-1],labels.copy()]
        variants[-1]['mask_packed'][0,1]=0 # Mask out native label macro15.
        for bad_labels in variants:
            self.write_labels(ep,bad_labels)
            with self.assertRaises(ValueError):evaluation.validate_dagger_label_output(self.job,**kwargs)
        self.assertEqual(ep.path.read_bytes(),before)

    def test_actor_context_must_remain_the_variant0_observation(self):
        ep,_=self.episode(context=False)
        with self.assertRaisesRegex(ValueError,'context'):
            evaluation.validate_dagger_label_output(self.job,executable_sha=sha(self.f.exe),log=self.native_log(),episode=ep)

if __name__=='__main__':unittest.main()
