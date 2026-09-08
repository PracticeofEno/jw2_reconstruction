"""Explicit, non-overwriting 528/542 -> current model and numeric Adam migration.

This utility does not create missing rollout observations, train a model, or
transfer BC admission. Fresh native observations are required for new features.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import tempfile

import numpy as np
import torch

import ranker_commander_model as model


def _sha(path):
    value = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def _expand_slot(name, value, parameter):
    if tuple(value.shape) == tuple(parameter.shape):
        return value.clone()
    expanded = torch.zeros_like(parameter, memory_format=torch.contiguous_format)
    if name == "vector1.weight" and value.shape == (256, value.shape[1]) and value.shape[1] in (528, 542):
        expanded[:, :value.shape[1]] = value
    elif name == "conv1.weight" and tuple(value.shape) == (16, 9, 3, 3):
        expanded[:, :9] = value
    else:
        raise ValueError(f"unsupported optimizer shape migration: {name}")
    return expanded


def upgrade_optimizer(source_policy, upgraded_policy, path):
    """Preserve a single-group numeric Adam by original parameter name/index.

    The source format records positional IDs, not names. Consequently reordered
    or multiple source groups cannot be inferred safely and are rejected.
    """
    old = list(source_policy.named_parameters())
    new = list(upgraded_policy.named_parameters())
    if len(old) != 39 or [name for name, _ in new[:39]] != [name for name, _ in old]:
        raise ValueError("source parameter names/order do not match the original 39 tensors")
    with np.load(path, allow_pickle=False) as data:
        if int(data["schema_crc"]) != source_policy.schema_crc or int(data["weight_version"]) != source_policy.weight_version:
            raise ValueError("source optimizer schema/version mismatch")
        metadata = json.loads(data["metadata"].tobytes().decode("utf-8"))
        groups = metadata["param_groups"]
        if len(groups) != 1 or groups[0]["params"] != list(range(len(old))):
            raise ValueError("optimizer upgrade requires the original single ordered Adam group")
        options = {key: value for key, value in groups[0].items() if key != "params"}
        optimizer = torch.optim.Adam([{"params": [parameter for _, parameter in new], **options}])
        expected_files = {"schema_crc", "weight_version", "metadata"}
        source_steps = {}
        for text_index, slots in metadata["state"].items():
            index = int(text_index)
            if str(index) != text_index or not 0 <= index < len(old):
                raise ValueError("invalid source optimizer parameter ID")
            required = {"step", "exp_avg", "exp_avg_sq"}
            if options.get("amsgrad", False):
                required.add("max_exp_avg_sq")
            if len(slots) != len(set(slots)) or set(slots) != required:
                raise ValueError("incomplete or unsupported source Adam slots")
            name, parameter = new[index]
            state = {}
            for slot in slots:
                key = f"p{index}_{slot}"
                expected_files.add(key)
                values = data[key]
                expected_shape = () if slot == "step" else tuple(old[index][1].shape)
                if values.shape != expected_shape or values.dtype.kind != "f" or not np.isfinite(values).all():
                    raise ValueError("invalid source optimizer state shape/type/value")
                if slot == "step":
                    step = float(values)
                    if step < 0 or step != int(step):
                        raise ValueError("invalid source Adam step")
                    source_steps[name] = step
                    state[slot] = torch.from_numpy(values.copy())
                else:
                    if slot.endswith("sq") and np.any(values < 0):
                        raise ValueError("negative source Adam squared moment")
                    state[slot] = _expand_slot(name, torch.from_numpy(values.copy()), parameter)
            optimizer.state[parameter] = state
        if set(data.files) != expected_files:
            raise ValueError("unexpected source optimizer fields")
        for _, parameter in new[39:]:
            optimizer.state[parameter] = {"step": torch.tensor(0.0), "exp_avg": torch.zeros_like(parameter),
                                          "exp_avg_sq": torch.zeros_like(parameter)}
            if options.get("amsgrad", False):
                optimizer.state[parameter]["max_exp_avg_sq"] = torch.zeros_like(parameter)
    return optimizer, {"source_parameter_count": len(old), "new_parameter_count": len(new) - len(old),
                       "source_steps": source_steps, "new_parameter_steps": 0,
                       "source_group_options": options,
                       "migration": "Old slots copied exactly; vector columns/map channels appended with zero moments; new adapter states start at zero."}


def save_optimizer(optimizer, path, *, version):
    state = optimizer.state_dict()
    arrays = {"schema_crc": np.array(model.SCHEMA_CRC, dtype=np.uint32),
              "weight_version": np.array(version, dtype=np.uint32)}
    for index, slots in state["state"].items():
        for name, value in slots.items():
            arrays[f"p{index}_{name}"] = value.detach().cpu().numpy()
    arrays["metadata"] = np.frombuffer(json.dumps({"param_groups": state["param_groups"],
        "state": {str(index): list(slots) for index, slots in state["state"].items()}}).encode("utf-8"), dtype=np.uint8)
    with Path(path).open("xb") as stream:
        np.savez(stream, **arrays)


def migrate(source, destination, *, source_optimizer=None, adapter_seed=941):
    source, destination = Path(source).resolve(), Path(destination).resolve()
    outputs = [destination, Path(str(destination) + ".json")]
    if source_optimizer is not None:
        source_optimizer = Path(source_optimizer).resolve()
        outputs.append(Path(str(destination) + ".optimizer.npz"))
    # Reserve even an omitted optimizer sidecar: an old file cannot be attached
    # accidentally to the new model.
    reserved = outputs + [Path(str(destination) + ".optimizer.npz")]
    if any(path.exists() for path in reserved):
        raise FileExistsError("upgrade outputs must be new, including sidecars")
    hashes = {str(source): _sha(source)}
    if source_optimizer is not None:
        hashes[str(source_optimizer)] = _sha(source_optimizer)
    source_sidecar = Path(str(source) + ".json")
    source_metadata = {}
    if source_sidecar.is_file():
        hashes[str(source_sidecar)] = _sha(source_sidecar)
        source_metadata = json.loads(source_sidecar.read_text(encoding="utf-8"))
        if source_metadata.get("weights_sha256", hashes[str(source)]) != hashes[str(source)]:
            raise ValueError("source metadata does not match source weights")
    policy = model.load_weights(source, allow_legacy=True)
    upgraded = model.upgrade_policy(policy, adapter_seed=adapter_seed)
    optimizer, optimizer_proof = (None, None) if source_optimizer is None else upgrade_optimizer(policy, upgraded, source_optimizer)
    destination.parent.mkdir(parents=True, exist_ok=True)
    published = []
    with tempfile.TemporaryDirectory(prefix="commander_upgrade_", dir=destination.parent) as temporary:
        temporary = Path(temporary)
        weights = temporary / "weights.bin"
        model.export_weights(upgraded, weights)
        restored = model.load_weights(weights)
        if any(not torch.equal(value, restored.state_dict()[name]) for name, value in upgraded.state_dict().items()):
            raise ValueError("upgraded weight reload mismatch")
        record = {"mode": "unapproved_model_upgrade", "schema_crc": model.SCHEMA_CRC,
                  "architecture": model.ARCHITECTURE, "weight_format_version": model.FORMAT_VERSION,
                  "weight_version": upgraded.weight_version, "weights_sha256": _sha(weights),
                  "source": str(source), "source_schema_crc": policy.schema_crc,
                  "source_vector_size": policy.vector_size, "vector_size": model.VECTOR_SIZE,
                  "source_map_shape": list(policy.map_shape), "map_shape": list(model.MAP_SHAPE),
                  "adapter_seed": adapter_seed, "source_admission_migrated": False,
                  "bc_gate": {"passed": False, "gameplay_passed": False}, "requires_revalidation": True,
                  "source_training_config": source_metadata.get("training_config"),
                  "initialization": "Existing tensors preserved; new vector columns and map channels zero; adapter down seeded, up zero.",
                  "gradient_initialization": "Zero-up gives zero down-gradient on the first backward; down remains trainable after up changes.",
                  "rollout_requirement": "Fresh native rollouts required for new observations; historical padding is not training data.",
                  "optimizer": optimizer_proof, "input_hashes": hashes}
        pairs = [(weights, destination)]
        if optimizer is not None:
            optimizer_file = temporary / "optimizer.npz"
            save_optimizer(optimizer, optimizer_file, version=upgraded.weight_version)
            record["optimizer_sha256"] = _sha(optimizer_file)
            pairs.append((optimizer_file, outputs[-1]))
        metadata_file = temporary / "metadata.json"
        metadata_file.write_text(json.dumps(record, indent=2, allow_nan=False) + "\n", encoding="utf-8")
        pairs.append((metadata_file, outputs[1]))
        if any(_sha(path) != expected for path, expected in hashes.items()):
            raise ValueError("source inputs changed during upgrade")
        try:
            for original, target in pairs:
                os.link(original, target)  # atomic create, refuses concurrent overwrite
                published.append(target)
        except BaseException:
            for path in published:
                path.unlink()
            raise
    return record


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--source-optimizer", type=Path)
    parser.add_argument("--adapter-seed", type=int, default=941)
    args = parser.parse_args(argv)
    torch.set_num_threads(1)
    record = migrate(args.source, args.out, source_optimizer=args.source_optimizer, adapter_seed=args.adapter_seed)
    print(json.dumps(record, indent=2, allow_nan=False))


if __name__ == "__main__":
    main()
