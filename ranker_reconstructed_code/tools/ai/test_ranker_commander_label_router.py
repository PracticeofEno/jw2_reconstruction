"""Label-only model migration contract tests; native games never launch."""
import copy
import json
from pathlib import Path
import unittest
from unittest.mock import patch

import ranker_commander_eval as evaluation
import ranker_commander_label_router as labels
import ranker_commander_strategy_router as router
from ranker_commander_strategy import sha
import test_ranker_commander_strategy_router as fixtures


class LabelModelMigrationTest(unittest.TestCase):
    def setUp(self):
        f=fixtures.StrategyRouterTest('test_public_race_only_and_baseline_child_is_allowed')
        f.setUp();self.addCleanup(f.doCleanups);self.f=f
        self.root=f.directory/'label_migration';self.root.mkdir()
        def file(name,raw):
            path=self.root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(raw);return path
        self.file=file
        self.exe=file('new/ranker_rebuild.exe',b'Synthetic new runtime; never executed')
        old_obj=file('old/model.obj',b'old synthetic model')
        new_obj=file('new/model.obj',b'new synthetic model')
        commander=file('old/ranker_ai_commander.obj',b'unchanged teacher and executor')
        engine=file('old/engine.obj',b'unchanged engine')
        self.engine=engine
        args=['-static',str(commander),str(engine),str(old_obj),'-o',str(f.exe)]
        quote=lambda xs:'\n'.join('"'+x.replace('\\','/')+'"'for x in xs).encode()
        old_rsp=file('old/native.rsp',quote(args))
        args[3]=str(new_obj);args[-1]=str(self.exe)
        new_rsp=file('new/native.rsp',quote(args));self.new_rsp=new_rsp
        source_model=file('old/model.cpp',b'original model\n')
        target_model=file('new/model.cpp',b'extended model\n')
        edits=self.write('edits.json',[dict(before='original model',after='extended model')])
        header=file('ranker_ai_commander_model.h',b'Synthetic fixed ABI header')
        build=self.write('build.json',dict(parent_executable_sha256=sha(f.exe),executable_sha256=sha(self.exe),
            parent_model_object_sha256=sha(old_obj),native_model_object_sha256=sha(new_obj),games_launched=0,deployed=False,
            base_native_model_source_sha256=sha(source_model),native_model_source_sha256=sha(target_model),exact_source_replacements_sha256=sha(edits)))
        parity=self.write('parity.json',dict(passed=True,executable_sha256=sha(self.exe),native_model_sha256=sha(target_model),
            format2_and_zero3_parent_native_bytes_exact=True,nonelf_native_bytes_exact=True,skill_logits_and_critic_exact=True,games_launched=0))
        self.proof=dict(kind='commander_model_only_label_runtime_migration',label_only=True,source_router=self.pin(f.manifest),
            source_runtime=self.pin(f.exe),target_runtime=self.pin(self.exe),source_response=self.pin(old_rsp),target_response=self.pin(new_rsp),
            source_model_object=self.pin(old_obj),target_model_object=self.pin(new_obj),common_link_inputs=[self.pin(commander),self.pin(engine)],
            source_model=self.pin(source_model),target_model=self.pin(target_model),source_replacements=self.pin(edits),model_header=self.pin(header),
            build_manifest=self.pin(build),native_model_parity=self.pin(parity),new_games=0,teacher_code_changed=False,engine_changed=False,numeric_profiles_changed=False)
        self.proof_path=self.write('proof.json',self.proof)
        self.definition=dict(kind=labels.KIND,method=labels.METHOD,purpose='dagger_labels_only',own_tribe=1,schema_crc=2129581458,
            input='public_opponent_tribe',executable=str(self.exe),executable_sha256=sha(self.exe),source_router=self.pin(f.manifest),
            routes=copy.deepcopy(f.binding['definition']['routes']),model_only_migration=self.pin(self.proof_path),new_performance_games=0,policy_adopted=False)
        self.path=self.write('label_router.json',self.definition)
        self.binding=labels.load_label_router(self.path)

    @staticmethod
    def pin(path):return dict(path=str(Path(path).resolve()),sha256=sha(path))

    def write(self,name,value):
        path=self.root/name;path.write_text(json.dumps(value),encoding='utf-8');return path

    def reload(self):
        self.write('proof.json',self.proof)
        self.definition['model_only_migration']=self.pin(self.proof_path)
        self.write('label_router.json',self.definition)
        return labels.load_label_router(self.path)

    def log(self,tribe=1):
        d=self.f.children[tribe]['definition']
        return '[ElfStrategyProfile] crc=%08x values=%s path=%s'%(d['profile_crc32'],','.join(format(v,'.17g')for v in d['parameters']),d['profile_path'])

    def job(self):
        return {**self.f.job(1),'dagger':True,'dagger_label_router':self.binding,'dagger_label_strategy':self.f.children[1]}

    def test_exact_source_routes_and_distinct_actual_runtime_are_retained(self):
        for tribe in range(4):
            child=labels.select_label_strategy(self.binding,tribe)
            self.assertEqual(child,self.f.children[tribe])
            result=labels.validate_label_profile(self.binding,tribe,child,executable_sha=sha(self.exe),log=self.log(tribe))
            self.assertEqual(result['executable_sha256'],sha(self.f.exe))
        self.assertNotEqual(self.binding['definition']['executable_sha256'],sha(self.f.exe))
        for invalid in (True,-1,4,'1'):
            with self.assertRaises(ValueError):labels.select_label_strategy(self.binding,invalid)

    def test_ordinary_router_and_actor_strategy_paths_reject_label_only_migration(self):
        with self.assertRaises(ValueError):router.load_router(self.path)
        with self.assertRaises(ValueError):
            labels.validate_label_profile(self.binding,1,self.f.children[1],executable_sha=sha(self.f.exe))
        with self.assertRaises(ValueError):
            evaluation.validate_dagger_label_job(self.job(),teacher=True,weights2=None,executable_sha=sha(self.exe))

    def test_same_runtime_delegation_remains_exact(self):
        binding=labels.load_label_router(self.f.manifest)
        self.assertEqual(binding,self.f.binding)
        self.assertEqual(labels.select_label_strategy(binding,1),self.f.children[1])
        self.assertEqual(labels.validate_label_profile(binding,1,self.f.children[1],executable_sha=sha(self.f.exe),log=self.log()),self.f.children[1]['definition'])

    def test_new_reward_claims_route_inputs_and_recursive_sources_are_rejected(self):
        for changes in (dict(new_performance_games=36),dict(policy_adopted=True),dict(input='seed'),dict(purpose='learned_evaluation')):
            original=copy.deepcopy(self.definition);self.definition.update(changes)
            with self.subTest(changes=changes),self.assertRaises(ValueError):self.reload()
            self.definition=original
        recursive=self.write('recursive.json',dict(self.definition))
        self.definition['source_router']=self.pin(recursive)
        with self.assertRaisesRegex(ValueError,'finite ordinary'):self.reload()

    def test_swapped_profile_and_runtime_proofs_are_rejected(self):
        self.definition['routes']['1']=copy.deepcopy(self.definition['routes']['2'])
        with self.assertRaisesRegex(ValueError,'routes changed'):self.reload()
        self.definition['routes']=copy.deepcopy(self.f.binding['definition']['routes'])
        self.proof['source_runtime']=self.pin(self.exe)
        with self.assertRaises(ValueError):self.reload()

    def test_modified_engine_or_added_link_input_is_rejected(self):
        original=self.engine.read_bytes();self.engine.write_bytes(b'changed engine')
        with self.assertRaises(ValueError):labels.validate_label_router(self.binding)
        self.engine.write_bytes(original)
        self.new_rsp.write_bytes(self.new_rsp.read_bytes()+b'\n"extra_engine.obj"')
        self.proof['target_response']=self.pin(self.new_rsp)
        with self.assertRaisesRegex(ValueError,'limited to model'):self.reload()

    def test_incomplete_common_input_and_false_compatibility_proof_are_rejected(self):
        original=copy.deepcopy(self.proof);self.proof['common_link_inputs'].pop()
        with self.assertRaisesRegex(ValueError,'incomplete'):self.reload()
        self.proof=original
        parity_path=Path(self.proof['native_model_parity']['path']);p=json.loads(parity_path.read_text(encoding='utf-8'))
        p['format2_and_zero3_parent_native_bytes_exact']=False;parity_path.write_text(json.dumps(p),encoding='utf-8')
        self.proof['native_model_parity']=self.pin(parity_path)
        with self.assertRaisesRegex(ValueError,'compatibility'):self.reload()

    def test_native_profile_log_is_verified_against_original_profile(self):
        opts=dict(teacher=False,weights2=None,executable_sha=sha(self.exe))
        d=evaluation.validate_dagger_label_job(self.job(),**opts,log=self.log())
        self.assertEqual(d,self.f.children[1]['definition'])
        for log in ('',self.log(2),self.log()+'\n'+self.log()):
            with self.subTest(log=log),self.assertRaises(ValueError):evaluation.validate_dagger_label_job(self.job(),**opts,log=log)
        changed={**self.job(),'dagger_label_strategy':self.f.children[2]}
        with self.assertRaisesRegex(ValueError,'public-opponent'):evaluation.validate_dagger_label_job(changed,**opts)

    def test_label_profile_only_enters_child_environment_with_neural_actor_flags(self):
        install=self.root/'install';install.mkdir();weights=self.file('weights.bin',b'placeholder, never loaded')
        with patch.object(evaluation.subprocess,'Popen',side_effect=OSError('test forbids native launch'))as launch:
            result=evaluation._run_game(install,weights,self.root/'jobs',1,self.job(),2120,
                teacher=False,deterministic=True,timeout=1,executable=self.exe)
        args=launch.call_args.args[0];env=launch.call_args.kwargs['env']
        self.assertIn('-AIDAGGER',args);self.assertNotIn('-AITEACHER',args)
        self.assertIn('-AIWEIGHTS:'+str(weights),args)
        self.assertEqual(env[evaluation.STRATEGY_ENV],self.f.children[1]['definition']['profile_path'])
        self.assertEqual(sha(args[0]),sha(self.exe))
        self.assertFalse(result['teacher']);self.assertTrue(result['neural_actor']);self.assertTrue(result['label_controller_only'])


if __name__=='__main__':unittest.main()
