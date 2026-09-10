"""Explicit migration of commander observations and weights to context schema 2.

Legacy recordings carry executed actions, while DAgger sidecars carry labels.
Only executed actions can reconstruct the four transfer clocks. No migration
overwrites its inputs, infers an unrecorded teacher style, or transfers an old
checkpoint's admission/optimizer state.

This historical utility still produces the 542-vector/9-map context contract.
The current 606-vector/12-map learner needs newly collected observations;
execution progress and the additional map channels cannot be inferred here.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import tempfile

import numpy as np

import ranker_commander_rollout as rollout

LEGACY_VECTOR_SIZE = 528
CONTEXT_VECTOR_SIZE = 542
TRANSFER_AGE_SLICE = slice(528, 532)
TRANSFER_EVER_SLICE = slice(532, 536)
TEACHER_SLICE = slice(536, 542)
CONTEXT_SIZE = 14
TRANSFER_SOURCES = (0, 1, 0, 2)


@dataclass(frozen=True)
class TeacherParameters:
    opening_velocis: int = 2
    tower_frame: int = 0
    attack_ratio: float = 1.5
    expansion_shift: int = 0
    harass_period: int = 0
    target_priority: int = 0


def _u32(value, name):
    if isinstance(value, (bool, np.bool_)) or not isinstance(value, (int, np.integer)) or not 0 <= int(value) <= 0xFFFFFFFF:
        raise ValueError(f"{name} must be an unsigned 32-bit integer")
    return int(value)


def teacher_parameters(variant: int) -> TeacherParameters:
    """Mirror CommanderTeacherVariant, including uint32 wrap and float32 math."""
    variant = _u32(variant, "teacher variant")
    if variant == 0:
        return TeacherParameters()
    state = (variant * 2654435761 + 0x9E3779B9) & 0xFFFFFFFF

    def next_u32():
        nonlocal state
        state ^= (state << 13) & 0xFFFFFFFF
        state ^= state >> 17
        state ^= (state << 5) & 0xFFFFFFFF
        return state

    opening = 2 + next_u32() % 7
    tower = 2000 + next_u32() % 3001
    ratio = np.float32(.8) + np.float32(next_u32() % 8) * np.float32(.1)
    expansion = next_u32() % 6001 - 3000
    # The second draw is conditional in C++; taking it when disabled changes
    # target_priority for all variants whose harassment period is zero.
    harass = 0 if next_u32() % 3 == 0 else 2000 + next_u32() % 4001
    priority = next_u32() % 3
    return TeacherParameters(opening, tower, float(ratio), expansion, harass, priority)


def encode_context(frame: int, last_transfer_frames, teacher_variant: int) -> np.ndarray:
    """Encode the predecision context in native vector slots 528 through 541.

    A never-used transfer has age frame / 6000 and a separate zero ever flag.
    The returned float32 values are rounded to binary16 by the RLO writer.
    """
    frame = _u32(frame, "frame")
    last = list(last_transfer_frames)
    if len(last) != 4:
        raise ValueError("exactly four last transfer frames are required")
    last = np.asarray([_u32(value, "last transfer frame") for value in last], dtype=np.int64)
    if np.any(last > frame):
        raise ValueError("last transfer frame is later than observation")
    params = teacher_parameters(teacher_variant)
    result = np.empty(CONTEXT_SIZE, dtype=np.float32)
    result[:4] = np.clip((frame - last).astype(np.float32) / np.float32(6000), 0, 1)
    result[4:8] = last > 0
    result[8:] = np.asarray([
        params.opening_velocis, params.tower_frame, params.attack_ratio,
        params.expansion_shift, params.harass_period, params.target_priority,
    ], dtype=np.float32) / np.asarray([8, 5000, 1.5, 3000, 6000, 2], dtype=np.float32)
    return result


def migrate_episode(episode: rollout.Episode, *, teacher_variant: int) -> rollout.Episode:
    """Reconstruct context before DAgger relabeling; retain the source path.

    The previous-action features independently check that the supplied actions
    are the actions the executor actually processed. Midgame recordings and
    already relabeled episodes cannot establish this history safely.
    """
    teacher_parameters(teacher_variant)
    source = episode.records
    if source.dtype["vector"].shape != (LEGACY_VECTOR_SIZE,):
        raise rollout.RolloutError("context migration requires legacy 528-feature observations")
    rollout._validate_records(source)
    vectors = source["vector"]
    if int(source["frame"][0]) != 1 or np.any(vectors[0, 518:527] != 0):
        raise rollout.RolloutError("context migration requires a fresh commander recording starting at frame 1")
    expected_previous = (source["action"][:-1].astype(np.float32) /
                         np.asarray(rollout.LEGACY_HEAD_SIZES, dtype=np.float32))
    expected_previous = expected_previous.astype(np.float16).astype(np.float32)
    if not np.array_equal(vectors[1:, 518:526], expected_previous):
        raise rollout.RolloutError("previous-action history differs from executed actions; migrate before DAgger relabeling")
    if np.any(vectors[:, (166, 187, 208)] < 0):
        raise rollout.RolloutError("negative squad count cannot reconstruct transfer history")
    result = np.zeros(len(source), dtype=rollout._record_dtype(False, CONTEXT_VECTOR_SIZE, rollout.LEGACY_MAP_SIZE))
    if result.dtype["vector"].shape != (CONTEXT_VECTOR_SIZE,):
        raise RuntimeError("commander rollout module does not implement the context schema")
    for name in result.dtype.names:
        if name not in ("vector", "crc32"):
            result[name] = source[name]
    result["vector"][:, :LEGACY_VECTOR_SIZE] = vectors
    last = [0, 0, 0, 0]
    for index, frame in enumerate(source["frame"]):
        # Match the values observed by native inference and compact RLO IO.
        result["vector"][index, LEGACY_VECTOR_SIZE:] = encode_context(
            int(frame), last, teacher_variant).astype(np.float16).astype(np.float32)
        if index == len(source) - 1:
            break
        macro = int(source["action"][index, 0])
        if 38 <= macro <= 41:
            # _validate_records already rejected masked recorded actions.
            # The executor only advances a clock when at least one unit moves.
            squad = TRANSFER_SOURCES[macro - 38]
            if vectors[index, 166 + 21 * squad] > 0:
                last[macro - 38] = int(frame)
    rollout._validate_records(result)
    return rollout.Episode(episode.path, episode.owner, episode.seed, episode.weight_version, result)


def _digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def variant_from_job(path: Path, episode: rollout.Episode) -> tuple[int, dict]:
    """Read explicit style or the default in a verified recorded game command."""
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict) or data.get("seed") != episode.seed:
        raise ValueError("job metadata must match the rollout policy seed")
    if data.get("valid") is not True:
        raise ValueError("job metadata must describe a validated completed game")
    owner_suffix = "2" if episode.owner == 2 else ""
    if episode.owner not in (1, 2):
        raise ValueError("job metadata supports only commander owners 1 and 2")
    source_key = "rollout2" if owner_suffix else "rollout"
    if not isinstance(data.get(source_key), str) or Path(data[source_key]).resolve() != episode.path.resolve():
        raise ValueError("job metadata must identify the exact source rollout path")
    style_key = "teacher_variant" + owner_suffix
    explicit = data.get(style_key)
    command = data.get("command")
    recorded = None
    if isinstance(command, list) and command and all(isinstance(item, str) for item in command):
        recorded_rollouts = [item[len("-AIROLLOUT:"):] for item in command if item.startswith("-AIROLLOUT:")]
        if ("-AICOMMANDER" not in command or len(recorded_rollouts) != 1 or
                not isinstance(data.get("rollout"), str) or
                Path(recorded_rollouts[0]).resolve() != Path(data["rollout"]).resolve()):
            raise ValueError("job command does not identify a commander rollout")
        prefix = "-AITEACHERVAR" + owner_suffix + ":"
        found = [item[len(prefix):] for item in command if item.startswith(prefix)]
        if len(found) > 1:
            raise ValueError("job command specifies multiple teacher variants")
        recorded = _u32(int(found[0]), "teacher variant") if found else 0
    if explicit is None and recorded is None:
        raise ValueError("legacy job has no verifiable teacher variant; supply --teacher-variant")
    variant = _u32(explicit, "teacher variant") if explicit is not None else recorded
    if recorded is not None and recorded != variant:
        raise ValueError("job teacher variant disagrees with recorded command")
    return variant, {"path": str(path.resolve()), "sha256": _digest(path), "data": data}


def _publish_files(files: list[tuple[Path, Path]]):
    """Publish complete files exclusively, rolling back only links we created."""
    created = []
    try:
        for temporary, destination in files:
            os.link(temporary, destination)
            created.append(destination)
    except BaseException:
        for destination in reversed(created):
            destination.unlink()
        raise


def _destinations(source: Path, destination: Path, labels: bool = False, *, checkpoint: bool = False):
    source, destination = source.resolve(), destination.resolve()
    if source == destination:
        raise ValueError("migration destination must differ from source")
    paths = [destination, Path(str(destination) + ".context.json")]
    if labels:
        paths.append(Path(str(destination) + ".teacher.bin"))
    # Consumers discover these sidecars by basename. Even when this migration
    # does not write them, an orphan from an earlier artifact must not become
    # attached to a newly converted rollout or checkpoint.
    reserved = paths + ([Path(str(destination) + suffix) for suffix in (".json", ".optimizer.npz")]
                        if checkpoint else [Path(str(destination) + ".teacher.bin")])
    if any(path.exists() for path in reserved):
        raise FileExistsError("migration never overwrites an existing destination or sidecar")
    destination.parent.mkdir(parents=True, exist_ok=True)
    return paths


def migrate_rollout_file(source, destination, *, teacher_variant=None, job=None) -> dict:
    source, destination = Path(source).resolve(), Path(destination).resolve()
    if (teacher_variant is None) == (job is None):
        raise ValueError("specify exactly one of teacher_variant or job metadata")
    labels = Path(str(source) + ".teacher.bin")
    outputs = _destinations(source, destination, labels.exists())
    episode = rollout.read_rollout(source, allow_legacy=True)
    try:
        job_provenance = None
        if job is not None:
            teacher_variant, job_provenance = variant_from_job(Path(job), episode)
        converted = migrate_episode(episode, teacher_variant=teacher_variant)
        if labels.exists():
            # Check sidecar count, mask legality and actions without allowing
            # labels to affect the already reconstructed executed history.
            rollout.relabel_with_teacher(converted)
        metadata = {
            "mode": "context_migration", "kind": "rollout", "source": str(source),
            "source_sha256": _digest(source), "destination": str(destination),
            "source_vector_size": LEGACY_VECTOR_SIZE, "vector_size": CONTEXT_VECTOR_SIZE,
            "schema_crc": rollout.CONTEXT_SCHEMA_CRC, "format_version": 3,
            "current_training_compatible": False,
            "teacher_variant": int(teacher_variant), "records": len(converted.records),
            "owner": episode.owner, "seed": episode.seed, "weight_version": episode.weight_version,
            "history": "executed RLO actions before DAgger relabeling; initial frame 1",
            "teacher_labels_sha256": _digest(labels) if labels.exists() else None,
            "job_provenance": job_provenance,
        }
        with tempfile.TemporaryDirectory(prefix="commander_context_", dir=destination.parent) as temporary:
            temporary = Path(temporary)
            output = temporary / "rollout.rlo"
            rollout.write_context_rollout(output, converted.records, owner=episode.owner,
                                  seed=episode.seed, weight_version=episode.weight_version)
            metadata["destination_sha256"] = _digest(output)
            info = temporary / "context.json"
            info.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
            files = [(output, outputs[0]), (info, outputs[1])]
            if labels.exists():
                copied_labels = temporary / "labels.bin"
                copied_labels.write_bytes(labels.read_bytes())
                files.append((copied_labels, outputs[2]))
            _publish_files(files)
        return metadata
    finally:
        episode.close()


def migrate_weights_file(source, destination) -> dict:
    import ranker_commander_model as model

    source, destination = Path(source).resolve(), Path(destination).resolve()
    outputs = _destinations(source, destination, checkpoint=True)
    policy = model.load_weights(source, allow_legacy=True)
    if policy.vector1.in_features != LEGACY_VECTOR_SIZE:
        raise ValueError("weight migration requires a legacy 528-feature checkpoint")
    upgraded = model.upgrade_legacy_policy(policy)
    metadata = {
        "mode": "context_migration", "kind": "weights", "source": str(source),
        "source_sha256": _digest(source), "destination": str(destination),
        "source_vector_size": LEGACY_VECTOR_SIZE, "vector_size": CONTEXT_VECTOR_SIZE,
        "schema_crc": rollout.CONTEXT_SCHEMA_CRC, "weight_version": upgraded.weight_version,
        "current_training_compatible": False,
        "initialization": "legacy tensors preserved; 14 new input columns initialized to zero",
        "requires_bc_revalidation": True,
        "optimizer": "fresh optimizer required; legacy optimizer state and admission not migrated",
    }
    with tempfile.TemporaryDirectory(prefix="commander_context_", dir=destination.parent) as temporary:
        temporary = Path(temporary)
        output, info = temporary / "weights.bin", temporary / "context.json"
        model.export_context_weights(upgraded, output, upgraded.weight_version)
        metadata["destination_sha256"] = _digest(output)
        info.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
        _publish_files([(output, outputs[0]), (info, outputs[1])])
    return metadata


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="kind", required=True)
    weights = subparsers.add_parser("weights", help="preserve legacy policy outputs and add zero context weights")
    episodes = subparsers.add_parser("rollout", help="reconstruct transfer history from a complete legacy RLO")
    for command in (weights, episodes):
        command.add_argument("source", type=Path)
        command.add_argument("destination", type=Path)
    style = episodes.add_mutually_exclusive_group(required=True)
    style.add_argument("--teacher-variant", type=int)
    style.add_argument("--job", type=Path, help="verified original game job.json containing style provenance")
    args = parser.parse_args(argv)
    if args.kind == "weights":
        result = migrate_weights_file(args.source, args.destination)
    else:
        result = migrate_rollout_file(args.source, args.destination,
                                      teacher_variant=args.teacher_variant, job=args.job)
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
