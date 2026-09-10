"""Identity of a reward-searched Elf strategy over a rule action executor.

The low-level rollout remains a teacher rollout. This is not an NN checkpoint
or a PPO trajectory. Parameters are sampled/selected using actual match wins.
"""
from pathlib import Path
import hashlib,json,math,re,zlib

KEYS=('worker_delta','red_scale_delta','frontline_scale_delta','mana_delta',
      'attack_army_delta','attack_ratio_delta')
LOW=(-4,-.25,-.5,-1,-8,-.3)
HIGH=(4,.5,1,4,8,.3)
INTEGRAL=(0,3,4)
KIND='reward_optimized_elf_strategy'
ENV='JW2_ELF_STRATEGY_PROFILE'
PATTERN=re.compile(r'\[ElfStrategyProfile\] crc=([0-9a-fA-F]{8}) values=([^\s]+) path=(.+)')

def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def parameters(values):
    values=[float(v) for v in values]
    if len(values)!=6 or any(not math.isfinite(v) or not LOW[i]<=v<=HIGH[i] for i,v in enumerate(values)):
        raise ValueError('invalid Elf strategy parameter bounds')
    if any(values[i]!=int(values[i]) for i in INTEGRAL):raise ValueError('nonintegral Elf strategy count')
    return values

def parse_profile(raw):
    if not 0<len(raw)<=4096 or b'\x00' in raw:raise ValueError('invalid Elf profile bytes')
    words=raw.decode('ascii').split()
    if len(words)!=7 or words[0]!='JWELFSTRAT1':raise ValueError('invalid Elf profile format')
    return parameters(words[1:])

def validate_strategy(binding, *, executable_sha=None, log=None):
    path=Path(binding['manifest'])
    if sha(path)!=binding['manifest_sha256']:raise ValueError('strategy manifest changed')
    d=json.loads(path.read_text(encoding='utf-8'))
    if d!=binding['definition'] or d['kind']!=KIND:raise ValueError('strategy definition changed')
    if d['own_tribe']!=1 or d['schema_crc']!=2129581458 or not d['low_level_teacher']:
        raise ValueError('strategy race/rollout contract differs')
    if d['purpose'] not in ('baseline','search','learned_evaluation'):raise ValueError('unknown strategy purpose')
    if executable_sha is not None and executable_sha!=d['executable_sha256']:
        raise ValueError('strategy executor changed')
    raw=Path(d['profile_path']).read_bytes()
    if hashlib.sha256(raw).hexdigest()!=d['profile_sha256']:raise ValueError('strategy profile changed')
    if zlib.crc32(raw)!=d['profile_crc32'] or parse_profile(raw)!=d['parameters']:
        raise ValueError('strategy numeric profile differs')
    if log is not None:
        rows=PATTERN.findall(log)
        if len(rows)!=1:raise ValueError('native strategy profile receipt missing or ambiguous')
        crc,values,loaded_path=rows[0]
        if int(crc,16)!=d['profile_crc32'] or parameters(values.split(','))!=d['parameters']:
            raise ValueError('native strategy bytes/values differ')
        if Path(loaded_path.strip()).resolve()!=Path(d['profile_path']).resolve():
            raise ValueError('native strategy path differs')
    return d

def load_strategy(path, *, executable_sha):
    path=Path(path).resolve()
    binding=dict(manifest=str(path),manifest_sha256=sha(path),definition=json.loads(path.read_text(encoding='utf-8')))
    validate_strategy(binding,executable_sha=executable_sha)
    return binding

def write_strategy(directory, values, executable, *, purpose, provenance=None):
    directory=Path(directory).resolve();directory.mkdir(parents=True,exist_ok=False)
    values=parameters(values);raw=('JWELFSTRAT1\n'+' '.join(format(v,'.17g') for v in values)+'\n').encode('ascii')
    profile=directory/'profile.txt';profile.write_bytes(raw)
    definition=dict(kind=KIND,method='CEM',own_tribe=1,schema_crc=2129581458,
        low_level_teacher=True,neural_actor=False,ppo_eligible=False,purpose=purpose,
        keys=list(KEYS),parameters=values,profile_path=str(profile),profile_sha256=sha(profile),
        profile_crc32=zlib.crc32(raw),executable=str(Path(executable).resolve()),
        executable_sha256=sha(executable),provenance=provenance or {})
    manifest=directory/'controller.json';manifest.write_text(json.dumps(definition,indent=2)+'\n',encoding='utf-8')
    return load_strategy(manifest,executable_sha=definition['executable_sha256'])
