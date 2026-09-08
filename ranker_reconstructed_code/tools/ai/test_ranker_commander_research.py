"""Focused research resume, native provenance, diagnostics and safe-epoch checks."""
from dataclasses import asdict
from contextlib import redirect_stdout
import copy
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import numpy as np
import torch
import ranker_commander_research as research
from ranker_commander_model import CommanderPolicy
from ranker_commander_rollout import HEAD_OFFSETS, HEAD_SIZES, MAP_SHAPE, RECORD_DTYPE, SCHEMA_CRC, WIN, write_rollout
from ranker_commander_train import TrainConfig, save_checkpoint

torch.set_num_threads(1)


def write_json(path, data):
    Path(path).write_text(json.dumps(data), encoding="utf-8")


class ResearchTests(unittest.TestCase):
    def test_upgrade_and_research_resume_restore_explicit_settings(self):
        source = asdict(TrainConfig(mode="ppo", iteration=4, gamma=1, gae_lambda=.98,
                                   shaping_scale=.5, teacher_kl_initial=.05, teacher_kl_floor=.01, teacher_kl_decay=100))
        config, proof = research.restore_config({"mode": "unapproved_model_upgrade", "source_training_config": source})
        self.assertEqual((config.iteration, config.gamma, config.gae_lambda, config.shaping_scale), (5, 1, .98, .5))
        self.assertEqual((config.learning_rate_initial, config.learning_rate_final), (2.5e-5, 2.5e-5))
        self.assertFalse(proof["source_admission_migrated"])
        current = dict(source, iteration=5, gae_lambda=.97)
        config, proof = research.restore_config({"mode": "unapproved_research_ppo", "source_training_config": source,
            "training_config": current, "shaping_scale": .25}, iteration=0, gae_lambda=0, shaping_scale=0)
        self.assertEqual((config.iteration, config.gae_lambda, config.shaping_scale), (0, 0, 0))
        self.assertEqual(proof["settings_source"], "training_config")
        for metadata in ({}, {"training_config": {"iteration": 2}}):
            with self.assertRaisesRegex(ValueError, "saved research"):
                research.restore_config(metadata)
        with self.assertRaises(ValueError):
            research.restore_config({"source_training_config": source}, gamma=float("nan"))

    def fixture(self, directory):
        root = Path(directory).resolve()
        torch.manual_seed(19)
        policy = CommanderPolicy()
        optimizer = torch.optim.Adam(policy.parameters(), lr=1e-4)
        # Nonempty, unequal-to-default moments ensure resume is actually exercised.
        sum(parameter.square().sum() for parameter in policy.parameters()).backward()
        optimizer.step()
        config = TrainConfig(mode="ppo", iteration=4, gamma=1, gae_lambda=.98,
                             teacher_kl_initial=.05, teacher_kl_floor=.01, teacher_kl_decay=100)
        weights = root/"initial.bin"
        save_checkpoint(policy, weights, version=9, optimizer=optimizer, metadata={
            "mode": "unapproved_model_upgrade", "source_training_config": asdict(config)})
        teacher = root/"teacher.bin"
        save_checkpoint(policy, teacher, version=9, metadata={"mode": "bc_reference"})
        executable = root/"ranker_rebuild.exe"
        executable.write_bytes(b"synthetic native artifact; never executed")
        runtime = root/"runtime_contract.json"
        write_json(runtime, {"schema_crc": SCHEMA_CRC, "vector_size":606, "map_size":3072,
            "weight_format":2, "rollout_format":4, "rollout_record_bytes":4446, "executable":str(executable),
            "input_hashes":{str(executable):research.digest(executable)}})
        phase = root/"phase"
        phase.mkdir()
        output = phase/"native"
        output.mkdir()
        records = np.zeros(5, dtype=RECORD_DTYPE)
        records["frame"] = [32,64,96,128,160]
        records["status"][-1] = WIN
        records["terminal_reward"][-1] = 1.2
        records["vector"][:,542:] = .5
        records["map"][:,9*256:] = 128
        for offset,width in zip(HEAD_OFFSETS,HEAD_SIZES):
            records["mask"][:-1,offset:offset+width] = 1
        with torch.no_grad():
            predicted = policy.evaluate(torch.from_numpy(records["vector"][:-1].copy()),
                torch.from_numpy(records["map"][:-1].copy()).float().reshape(-1,*MAP_SHAPE)/255,
                torch.from_numpy(records["action"][:-1].copy()).long(),
                torch.from_numpy(records["mask"][:-1].copy()).bool(),
                torch.from_numpy(records["privileged"][:-1].copy()))
        records["logp"][:-1] = predicted["logp"].numpy()
        records["value"][:-1] = predicted["value"].numpy()
        rollout = output/"commander.rlo"
        write_rollout(rollout, records, owner=1, seed=7, weight_version=9)
        for name in ("job.json","Jw2.log","commander_metrics_1.json","ai_selfplay_result.json"):
            (output/name).write_text("{}", encoding="utf-8")
        job = {"job":0,"seed":7,"tribe":2,"start_pair":[0,1],"curriculum":2,"max_frames":60000,
               "opp_slow":0,"coordinated_transfers":False,"policy_seed":100}
        report = {**job,"rollout":str(rollout),"valid":True,"evaluation_valid":True,"teacher":False,
                  "deterministic":False,"diagnostic_only":False,"weights_sha256":research.digest(weights),
                  "weight_version":9,"policy_seed_verified":True,"coordinated_transfers_verified":True,
                  "decisions":4,"status":WIN,"end_frame":160,
                  "artifact_hashes":{str(p):research.digest(p) for p in output.iterdir()},
                  "command":[str(executable),f"-AIWEIGHTS:{weights}",f"-AIROLLOUT:{rollout}",
                             "-AIPOLICYSEED:100","-AICOORDINATEDTRANSFERS:0"]}
        plan = {"mode":"sample","deterministic":False,"teacher":False,"max_frames":60000,
                "weights":str(weights),"weight_version":9,"runtime_contract":str(runtime),
                "runtime_contract_sha256":research.digest(runtime),"executable":str(executable),
                "schema_crc":SCHEMA_CRC,"vector_size":606,"map_size":3072,"coordinated_transfers":False,
                "input_hashes":{str(weights):research.digest(weights)},"jobs":[job],"job_indices":[0]}
        write_json(phase/"plan.json",plan)
        write_json(phase/"evaluation.json",{"reports":[report]})
        self.close_phase(phase,runtime)
        args = research.parser().parse_args(["--runtime-contract",str(runtime),"--cohort",str(phase/"evaluation.json"),
            "--weights",str(weights),"--teacher",str(teacher),"--out",str(root/"training"),
            "--expected-games","1","--epochs","3","--minibatch","4"])
        return args, report

    def close_phase(self, phase, runtime):
        links = {"phase_plan_sha256":research.digest(phase/"plan.json"),
                 "evaluation_sha256":research.digest(phase/"evaluation.json"),"runtime_contract_sha256":research.digest(runtime)}
        result = {**links,"complete":True,"diagnostic_only":False,"full_condition_coverage":True,
                  "performance_games":1,"requested_jobs":[0]}
        write_json(phase/"results.json",result)
        write_json(phase/"state.json",{**links,"state":"complete","results_sha256":research.digest(phase/"results.json")})

    def test_real_checkpoint_three_epochs_preserve_optimizer_and_version(self):
        with tempfile.TemporaryDirectory() as directory:
            args,_ = self.fixture(directory)
            with redirect_stdout(io.StringIO()):
                result = research.run(args)
            self.assertTrue(result["complete"])
            self.assertEqual(result["selected_epoch"],3)
            self.assertEqual([row["weight_version"] for row in result["epochs"]],[10,10,10])
            self.assertEqual(result["iteration"],5)
            proof = research.read_json(args.out/"verification_epoch_003.json")
            self.assertTrue(proof["optimizer_reload_exact"])
            self.assertEqual(set(proof["adam_steps"].values()),{4})
            meta = research.read_json(Path(result["selected_weights"]+".json"))
            self.assertFalse(meta["source_admission_migrated"])
            self.assertFalse(meta["bc_gate"]["passed"])
            resumed,_ = research.restore_config(meta)
            self.assertEqual((resumed.iteration,resumed.gamma,resumed.gae_lambda),(6,1,.98))
            self.assertEqual(len(result["epochs"][2]["metrics"]["heads"]),8)
            self.assertIn("early_lt4000",result["epochs"][2]["metrics"]["critic"])

    def test_unsafe_first_epoch_is_saved_but_never_selected(self):
        with tempfile.TemporaryDirectory() as directory:
            args,_ = self.fixture(directory)
            actual = research.diagnostics
            calls = 0
            def exceed_after_initial(*values):
                nonlocal calls
                report = actual(*values)
                calls += 1
                if calls > 1: report["approximate_kl"] = .031
                return report
            with patch.object(research,"diagnostics",side_effect=exceed_after_initial),redirect_stdout(io.StringIO()):
                result = research.run(args)
            self.assertTrue(result["stopped_for_kl"])
            self.assertEqual(len(result["epochs"]),1)
            self.assertIsNone(result["selected_epoch"])
            self.assertFalse(result["eligible"])
            self.assertEqual(result["selected_weights"],str(args.weights))
            self.assertFalse(research.read_json(args.out/"epoch_001.bin.json")["eligible_for_selection"])
            initial={"epoch":0}
            self.assertEqual(research.select_safe(initial,[{"epoch":1,"eligible_for_selection":True},
                {"epoch":2,"eligible_for_selection":False}])["epoch"],1)

    def test_provenance_rejects_wrong_runtime_stale_weights_and_command(self):
        with tempfile.TemporaryDirectory() as directory:
            args, original = self.fixture(directory)
            phase = args.cohort.parent
            for mutation in (lambda row:row.update(weight_version=8),
                             lambda row:row.update(weights_sha256="0"*64),
                             lambda row:row["command"].append("-AIDETERMINISTIC"),
                             lambda row:row.update(policy_seed_verified=False)):
                row=copy.deepcopy(original);mutation(row)
                write_json(args.cohort,{"reports":[row]});self.close_phase(phase,args.runtime_contract)
                with self.assertRaises(ValueError):
                    research.verify_phase(args.runtime_contract,args.cohort,args.weights,9,1,{})
            write_json(args.cohort,{"reports":[original]});self.close_phase(phase,args.runtime_contract)
            with self.assertRaises(ValueError):
                research.verify_phase(args.runtime_contract,args.cohort,args.weights,9,2,{})
            Path(original["rollout"]).write_bytes(b"changed")
            with self.assertRaisesRegex(ValueError,"hash mismatch"):
                research.verify_phase(args.runtime_contract,args.cohort,args.weights,9,1,{})

    def test_verify_only_makes_no_checkpoint(self):
        with tempfile.TemporaryDirectory() as directory:
            args,_ = self.fixture(directory)
            args.verify_only=True
            with patch.object(research.training,"train_update",side_effect=AssertionError("must not train")):
                result=research.run(args)
            self.assertEqual(result["gradient_updates"],0)
            self.assertFalse(list(args.out.glob("*.bin")))

    def test_conditional_kl_separates_forced_and_active_and_macro_flip(self):
        count=2
        masks=torch.zeros(count,sum(HEAD_SIZES),dtype=torch.bool)
        for offset in HEAD_OFFSETS:masks[:,offset]=True
        masks[0,1]=True
        masks[0,HEAD_OFFSETS[1]+1]=True
        old={"logits":torch.zeros(count,sum(HEAD_SIZES)),"logp":torch.zeros(count,8),"value":torch.zeros(count)}
        new={name:value.clone() for name,value in old.items()}
        new["logits"][0,1]=2
        new["logits"][0,HEAD_OFFSETS[1]+1]=1
        new["logits"][1,HEAD_OFFSETS[1]+1]=100 # Illegal, cannot affect forced distribution.
        batch={"old_logp":torch.zeros(count),"target":torch.tensor([1.,-1.]),"advantage":torch.ones(count),"masks":masks}
        labels={"frame":np.array([32,5000]),"tribe":np.array([0,1]),"outcome":np.array([1,2]),"mc_target":np.array([1.,-1.])}
        report=research.diagnostics(new,old,batch,labels)
        self.assertEqual(report["macro_argmax_flips"],1)
        self.assertEqual(report["macro_transitions"]["old_noop_to_production"],1)
        self.assertEqual(report["macro_transitions"]["old_production_to_noop"],0)
        self.assertGreater(report["heads"][1]["active_choice_kl"]["mean"],0)
        self.assertEqual(report["heads"][1]["forced_choice_kl"]["max"],0)


if __name__ == "__main__":
    unittest.main()
