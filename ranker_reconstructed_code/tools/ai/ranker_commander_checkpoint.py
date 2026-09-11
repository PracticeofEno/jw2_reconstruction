"""Export, verify and relocate a local Commander PPO checkpoint."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import importlib.metadata
import json
from pathlib import Path, PurePosixPath
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[3]


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')


def digest(path, *, text=False):
    data = path.read_bytes()
    return hashlib.sha256(data.replace(b'\r\n', b'\n') if text else data).hexdigest()


def inside(root, relative):
    part = PurePosixPath(relative)
    if part.is_absolute() or '..' in part.parts or '\\' in relative or ':' in relative:
        raise ValueError(f'unsafe checkpoint path: {relative}')
    path = (root / relative).resolve()
    path.relative_to(root.resolve())
    return path


def pins(value):
    if isinstance(value, dict):
        if 'path' in value and 'sha256' in value:
            yield value
        for child in value.values():
            yield from pins(child)
    elif isinstance(value, list):
        for child in value:
            yield from pins(child)


def named_folders(contract, documents, candidates):
    """Group weights by purpose, race and version; keep Adam sidecars adjacent."""
    latest = list(documents.values())[-1]
    roles = {}
    for role, roster in (('current', latest.get('current', {})),
                         ('best', latest.get('best', {})), ('candidates', candidates)):
        for item in pins(roster):
            if str(item['path']).endswith('.bin'):
                assigned = roles.setdefault(str(Path(item['path']).resolve()), [])
                if role not in assigned:
                    assigned.append(role)
    races = ('primitive', 'elf', 'tyrano', 'demon')
    folders = {}
    for item in pins([contract, list(documents.values()), candidates]):
        path = Path(item['path']).resolve()
        if path.suffix != '.bin' or 'race' not in item or 'version' not in item:
            continue
        if str(path.parent) in folders:
            continue
        race, version = int(item['race']), int(item['version'])
        if race not in range(len(races)):
            raise ValueError(f'unknown race: {race}')
        role = '_'.join(roles.get(str(path), ['history']))
        folder = f'{role}/{race}_{races[race]}_v{version}'
        if folder in folders.values():
            folder += '_' + str(item['sha256'])[:8]
        folders[str(path.parent)] = folder
    return folders


def export(campaign, bundle):
    campaign, bundle = campaign.resolve(), bundle.resolve()
    contract = read(campaign / 'contract.json')
    if contract.get('scope') != 'all_policy_parameters':
        raise ValueError('expected a full-policy Commander PPO campaign')
    if bundle.exists():
        raise ValueError('use a new snapshot directory; existing snapshots are immutable')
    # selection.json is atomically published after a complete fit/evaluation.
    # Capture its contiguous prefix without locking or pausing the live trainer.
    documents = {'initial_selection.json': read(campaign / 'initial_selection.json')}
    completed = 0
    while (campaign / f'round_{completed:03d}/selection.json').exists():
        name = f'round_{completed:03d}/selection.json'
        documents[name] = read(campaign / name)
        completed += 1
    candidates = {}
    pending = campaign / f'round_{completed:03d}/fit/roster.json'
    if pending.exists():
        candidates = read(pending)
    manifest = dict(schema=1, created_utc=datetime.now(timezone.utc).isoformat(),
        source_root=str(ROOT), source_campaign=str(campaign),
        source_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        completed_rounds=completed, resume_round=completed, files={}, sources={}, documents={},
        packages={name: importlib.metadata.version(name) for name in ('torch', 'numpy')})
    folders = named_folders(contract, documents, candidates)

    def add(path, expected=None):
        path = Path(path).resolve()
        relative = path.relative_to(ROOT).as_posix()
        actual = digest(path)
        if expected is not None and actual != expected:
            raise ValueError(f'checkpoint input changed: {path}')
        if relative.startswith('ranker_reconstructed_code/'):
            manifest['sources'][relative] = digest(path, text=True)
            return
        if relative in manifest['files']:
            if manifest['files'][relative]['sha256'] != actual:
                raise ValueError(f'file changed during export: {path}')
            return
        # Keep checkpoints and their sidecars together without carrying long
        # experiment paths into a second checkout's Windows MAX_PATH budget.
        fallback = 'runtime' if path.suffix == '.exe' else f'records/{len(folders):03d}'
        folder = folders.setdefault(str(path.parent), fallback)
        stored = folder + '/' + path.name
        destination = inside(bundle, stored)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, destination)
        if digest(destination) != actual or digest(path) != actual:
            raise ValueError(f'file changed during export: {path}')
        manifest['files'][relative] = dict(stored=stored, sha256=actual, bytes=destination.stat().st_size)
        if path.suffix == '.bin':
            metadata_path = path.with_suffix('.bin.json')
            if metadata_path.exists():
                metadata = read(metadata_path)
                if metadata.get('weights_sha256') != actual:
                    raise ValueError(f'weight/metadata mismatch: {path}')
                add(metadata_path)
            optimizer = path.with_suffix('.bin.optimizer.npz')
            if optimizer.exists():
                add(optimizer)

    for item in pins([contract, list(documents.values()), candidates]):
        add(item['path'], item['sha256'])
    for name, value in {'contract.json': contract, **documents, 'pending_candidates.json': candidates}.items():
        stored = 'templates/' + name
        write(inside(bundle, stored), value)
        manifest['documents'][name] = dict(stored=stored, sha256=digest(inside(bundle, stored)))
    write(bundle / 'manifest.json', manifest)
    return verify(bundle, load=False)


def verify(bundle, *, load=True):
    bundle = bundle.resolve()
    manifest = read(bundle / 'manifest.json')
    if manifest['schema'] != 1:
        raise ValueError('unsupported checkpoint schema')
    for item in [*manifest['files'].values(), *manifest['documents'].values()]:
        path = inside(bundle, item['stored'])
        if digest(path) != item['sha256']:
            raise ValueError(f'checkpoint hash mismatch (copy the complete snapshot again): {path}')
    for relative, expected in manifest['sources'].items():
        if digest(inside(ROOT, relative), text=True) != expected:
            raise ValueError(f'training source differs from snapshot: {relative}')
    models = optimizers = 0
    if load:
        import torch
        from ranker_commander_model import load_weights
        from ranker_commander_full_ppo import full_policy_scope
        from ranker_commander_train import load_optimizer
        torch.set_num_threads(2)
        for relative, item in manifest['files'].items():
            if not relative.endswith('.bin'):
                continue
            policy = load_weights(inside(bundle, item['stored']))
            full_policy_scope(policy)
            models += 1
            if (optimizer_item := manifest['files'].get(relative + '.optimizer.npz')):
                optimizer = load_optimizer(torch.optim.Adam(policy.parameters()),
                    inside(bundle, optimizer_item['stored']), version=policy.weight_version)
                if not optimizer.state:
                    raise ValueError(f'empty optimizer state: {relative}')
                optimizers += 1
    return dict(files=len(manifest['files']), bytes=sum(x['bytes'] for x in manifest['files'].values()),
        completed_rounds=manifest['completed_rounds'], models_loaded=models, optimizers_loaded=optimizers)


def restore(bundle, directory, install):
    bundle, directory, install = bundle.resolve(), directory.resolve(), install.resolve()
    verify(bundle)
    if directory.exists():
        raise ValueError('restore into a new directory; never replace an active campaign')
    if not install.is_dir() or not (install / 'Maps').is_dir():
        raise ValueError('--install must contain the original game assets and Maps directory')
    manifest = read(bundle / 'manifest.json')
    origin = manifest['source_root']
    mapping = {str(Path(origin) / relative): str(inside(bundle, item['stored']))
               for relative, item in manifest['files'].items()}
    mapping.update({str(Path(origin) / relative): str(inside(ROOT, relative))
                    for relative in manifest['sources']})
    source_campaign = manifest['source_campaign']
    templates = {name: read(inside(bundle, item['stored'])) for name, item in manifest['documents'].items()}
    for name in templates:
        if name not in ('contract.json', 'pending_candidates.json'):
            mapping[str(Path(source_campaign) / name)] = str(inside(directory, name))
    mapping[templates['contract.json']['install']] = str(install)

    def relocate(value):
        if isinstance(value, str):
            return mapping.get(value, value)
        if isinstance(value, list):
            return [relocate(x) for x in value]
        if isinstance(value, dict):
            return {key: relocate(child) for key, child in value.items()}
        return value

    # Historical receipt paths remain provenance. Only models, optimizer state,
    # sources, runtime and resume documents are executable inputs to continuation.
    for name, value in templates.items():
        if name not in ('contract.json', 'pending_candidates.json'):
            write(inside(directory, name), relocate(value))
    contract = relocate(templates['contract.json'])
    contract.pop('parent_directory', None)
    for item in contract['pins']:
        item['sha256'] = digest(Path(item['path']))
    known = {item['path'] for item in contract['pins']}
    for relative, item in manifest['files'].items():
        path = mapping[str(Path(origin) / relative)]
        if path not in known:
            contract['pins'].append(dict(path=path, sha256=digest(Path(path))))
    write(directory / 'contract.json', contract)
    write(directory / 'checkpoint_origin.json', dict(bundle=str(bundle),
        manifest_sha256=digest(bundle / 'manifest.json'), resume_round=manifest['resume_round'],
        pending_candidates_preserved_only=bool(templates['pending_candidates.json'])))
    # Exercise the same source, weight and optimizer loading used by the trainer.
    from ranker_commander_full_ppo import FullPolicyCampaign
    from ranker_commander_improve import roster
    FullPolicyCampaign(directory).check()
    for name in templates:
        if name.endswith('selection.json'):
            selection = read(inside(directory, name))
            roster(selection['current'])
            roster(selection['best'])
    return dict(directory=str(directory), resume_round=manifest['resume_round'],
        command=['python', '-u', '-B', str(ROOT / 'ranker_reconstructed_code/tools/ai/ranker_commander_full_ppo.py'), str(directory)])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    save = commands.add_parser('export')
    save.add_argument('campaign', type=Path)
    save.add_argument('bundle', type=Path)
    check = commands.add_parser('verify')
    check.add_argument('bundle', type=Path)
    recover = commands.add_parser('restore')
    recover.add_argument('bundle', type=Path)
    recover.add_argument('--directory', type=Path, required=True)
    recover.add_argument('--install', type=Path, required=True)
    args = parser.parse_args()
    result = (export(args.campaign, args.bundle) if args.command == 'export' else
              verify(args.bundle) if args.command == 'verify' else
              restore(args.bundle, args.directory, args.install))
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
