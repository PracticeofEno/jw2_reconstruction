"""Read-only observer of a multirace campaign; persist Korean cohort reports.

This does not import or modify the trainer, weights, or gameplay runtime.
--watch keeps the report files current. Chat delivery needs an active assistant;
writing these files by itself does not schedule messages in a conversation.
"""
from __future__ import annotations

import argparse
import base64
from collections import Counter
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import sys
import subprocess
import time


WORKSPACE = Path(__file__).resolve().parents[3]
DEFAULT_WORK = WORKSPACE / "debug_artifacts/commander/skills_20260908/training_run"
RACES = {0: "primitive", 1: "elf", 2: "tyrano", 3: "demon"}
NAMES = {"primitive": "원시인", "elf": "엘프", "tyrano": "티라노", "demon": "데몬"}
TERMINAL = {"stopped", "cycle_limit", "error"}


def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def atomic_write(path, text):
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(text, encoding="utf-8")
    # Windows readers/indexers can briefly hold the destination without delete
    # sharing. Keep the old complete report available while retrying replacement.
    for attempt in range(8):
        try:
            temporary.replace(path)
            return
        except PermissionError:
            if attempt == 7:
                raise
            time.sleep(0.05 * (attempt + 1))


def stamp(path=None):
    return (datetime.fromtimestamp(path.stat().st_mtime).astimezone() if path
            else datetime.now().astimezone()).isoformat(timespec="seconds")


def owner_value(report, key):
    return report[key + "2" if report.get("train_owner", 1) == 2 else key]


def aggregate(reports):
    statuses = Counter(owner_value(r, "status") for r in reports)
    metrics = [owner_value(r, "commander_metrics") for r in reports]
    arrays = {key: [sum(m[key][i] for m in metrics) for i in range(46)]
              for key in ("ability_orders", "ability_effect_success", "ability_effect_failed")}
    totals = {key: sum(m[key] for m in metrics) for key in
              ("mask_violations", "silent_rejections", "morph_orders", "stance_orders", "transport_orders")}
    return dict(games=len(reports), wins=statuses[1], losses=statuses[2],
                truncated=statuses[3], invalid=sum(n for k, n in statuses.items() if k not in (1, 2, 3)),
                valid_games=sum(bool(r.get("valid")) for r in reports),
                skills=arrays, **totals)


def cohort_event(row, state):
    policy = Path(row["path"])
    meta = read_json(str(policy) + ".json")
    digest = hashlib.sha256(policy.read_bytes()).hexdigest()
    if digest != meta["weights_sha256"] or meta["weight_version"] != row["version"]:
        raise ValueError(f"checkpoint identity mismatch: {policy}")
    reports = read_json(meta["cohort"])
    counts = aggregate(reports)
    if counts["games"] != row["games"] or counts["wins"] != row["wins"]:
        raise ValueError(f"cohort count mismatch: {policy}")
    tribe = meta["own_tribe"]
    pre = state["races"][str(tribe)].get("pre_skill_updates", 0)
    version = row["version"]
    cohort_number = meta.get("cohort_number", version)
    skill_updates = meta.get("skill_updates", version - pre)
    next_eval = cohort_number % state["config"]["evaluate_every"] == 0
    text = (f"{NAMES[row['checkpoint']]} 코호트 {cohort_number} 완료 "
            f"(스킬 확장 후 {skill_updates}회차): "
            f"{counts['games']}경기 {counts['wins']}승 {counts['losses']}패, "
            f"시간 제한 미결 {counts['truncated']}경기. "
            f"학습 경기 승률 {counts['wins'] / max(1, counts['games']):.1%}. "
            f"가중치 v{version} 저장 및 해시 확인 완료. "
            f"스킬 명령 {sum(counts['skills']['ability_orders'])}회, "
            f"변신 {counts['morph_orders']}회, 태세 {counts['stance_orders']}회, "
            f"수송 {counts['transport_orders']}회. "
            f"금지 행동 {counts['mask_violations']}건 / 명령 거절 {counts['silent_rejections']}건. "
            + ("별도 평가 예정." if next_eval else "이번 코호트는 별도 평가 대상 회차가 아님."))
    return dict(id="cohort:" + str(policy), kind="cohort", time=stamp(Path(str(policy) + ".json")),
                race=row["checkpoint"], tribe=tribe, version=version, cohort_number=cohort_number,
                recovery_cohort=meta.get("recovery_cohort"), skill_updates=skill_updates,
                phase=meta["phase"], checkpoint=str(policy), sha256=digest,
                decisions=meta["decisions"], evaluation_due=next_eval,
                learning_rate=meta.get("lr"), approximate_kl=meta.get("approximate_kl"),
                text=text, **counts)


def evaluation_event(checkpoint):
    policy = Path(checkpoint["path"])
    path = policy.parent / "evaluation/evaluation.json"
    result = read_json(path)
    meta = read_json(str(policy) + ".json")
    if result["weights_sha256"] != meta["weights_sha256"]:
        raise ValueError(f"evaluation checkpoint mismatch: {path}")
    gate = result["gate"]
    race, version = checkpoint["checkpoint"], checkpoint["version"]
    text = (f"{NAMES[race]} v{version} 별도 평가 {gate['games']}경기 완료: "
            f"최우선 행동 선택 승률 {gate['argmax_win_rate']:.1%}, "
            f"확률적 행동 선택 승률 {gate['sampling_win_rate']:.1%}. "
            f"자기대전 전환 기준 {'충족' if gate['ready'] else '미충족'}. "
            f"금지 행동 {gate['mask_violations']}건.")
    return dict(id="evaluation:" + str(path), kind="evaluation", time=stamp(path),
                race=race, version=version, path=str(path), text=text, **gate)


def scan(work, events, state):
    seen = {e["id"] for e in events}
    new, checkpoints = [], {}
    log = work / "runner.stdout.log"
    for line in log.read_text(encoding="utf-8").splitlines():
        try:
            row = json.loads(line)
        except json.JSONDecodeError:
            continue  # An in-progress final log line will be retried next poll.
        event = None
        if "checkpoint" in row:
            checkpoints[row["checkpoint"]] = row
            if "cohort:" + row["path"] not in seen:
                event = cohort_event(row, state)
        elif "evaluation" in row and row["evaluation"] in checkpoints:
            checkpoint = checkpoints[row["evaluation"]]
            event_id = "evaluation:" + str(Path(checkpoint["path"]).parent / "evaluation/evaluation.json")
            if event_id not in seen:
                event = evaluation_event(checkpoint)
        elif "recovery_selection" in row:
            race = row["recovery_selection"]
            event_id = "recovery_selection:" + race
            if event_id not in seen:
                baseline, candidate = (row["measurements"][name] for name in ("baseline", "candidate"))
                choice = "보강 후보" if row["selected"] == "candidate" else "기존 모델"
                message = (f"{NAMES[race]} 내장 AI 상대 비교 완료: "
                    f"기존 모델 승률 {baseline['argmax_win_rate']:.1%} / {baseline['sampling_win_rate']:.1%}, "
                    f"보강 후보 {candidate['argmax_win_rate']:.1%} / {candidate['sampling_win_rate']:.1%} "
                    f"(최우선 선택 / 확률 선택, 각각 {baseline['games']}경기). "
                    f"후속 강화학습에 {choice} 사용. 자기대전 자동 전환은 비활성화.")
                event = dict(id=event_id, kind="recovery_selection", race=race, time=stamp(),
                             selected=row["selected"], measurements=row["measurements"], text=message)
        elif row.get("phase") == "selfplay" and "historical_fraction" in row:
            event_id = "phase:selfplay"
            if event_id not in seen:
                event = dict(id=event_id, kind="phase", time=stamp(),
                             text="네 종족이 전환 기준을 충족해 자기대전으로 전환: 과거 모델 80%, 기본 AI 20%.")
        if event:
            new.append(event)
            seen.add(event["id"])
    if state["status"] in TERMINAL:
        event_id = f"status:{state['started_utc']}:{state['status']}"
        if event_id not in seen:
            message = {"stopped": "중지 요청에 따라 학습 종료", "cycle_limit": "설정된 순회 횟수에 도달해 학습 종료",
                       "error": "학습 오류로 중단"}[state["status"]]
            new.append(dict(id=event_id, kind="status", time=stamp(work / "state.json"),
                            status=state["status"], text=message + ": " + (state.get("error") or "체크포인트 보존")))
    return new


def progress(work, state):
    active = state.get("active")
    current = dict(time=stamp(), status=state["status"], phase=state["phase"], trainer_pid=state.get("pid"))
    if active:
        race = RACES[active["tribe"]]
        current["race"] = race
        if "run" in active and "jobs" in active:
            games = work / race / active["run"] / "games"
            current.update(run=active["run"], jobs=len(active["jobs"]),
                           completed=len(list(games.glob("game_*/output/ai_selfplay_result.json"))))
        elif state["status"] == "recovery_comparison":
            policy = active["policy"]
            directory = Path(state["races"][str(active["tribe"])]["comparison_paths"][policy])
            current.update(policy=policy, jobs=state["config"]["evaluation_games"],
                           completed=len(list(directory.glob("*/game_*/output/ai_selfplay_result.json"))))
        elif "episodes" in active:
            current["offline_episodes"] = active["episodes"]
    elif state["status"] == "checkpoint_saved":
        # The trainer preserves its last checkpoint while the following
        # evaluation runs. Infer that read-only phase from the current policy's
        # directory instead of making a healthy evaluation look idle.
        pending = []
        for tribe, row in state["races"].items():
            checkpoint = Path(row["policy"])
            directory = checkpoint.parent / "evaluation"
            if (checkpoint.is_file() and directory.is_dir()
                    and not (directory / "evaluation.json").exists()):
                pending.append((checkpoint.stat().st_mtime_ns, int(tribe), row, directory))
        if pending:
            _, tribe, row, directory = max(pending, key=lambda item:item[0])
            completed = set()
            for receipt in directory.glob("*/game_*/output/job.json"):
                try:
                    job = read_json(receipt)
                except (OSError, ValueError):
                    continue
                if job.get("valid") and job.get("evaluation_valid"):
                    completed.add((job["seed"], job["deterministic"]))
            current.update(status="evaluating", race=RACES[tribe], version=row["version"],
                           directory=str(directory), jobs=state["config"]["evaluation_games"],
                           completed=len(completed))
    return current


def save_reports(directory, events):
    atomic_write(directory / "events.json", json.dumps(events, ensure_ascii=False, indent=2) + "\n")
    lines = ["# 종족별 학습 코호트 보고", "",
             "학습 경기 성적과 별도 평가 성적을 구분한다. 시간 제한 미결은 승·패에 포함하지 않는다.",
             "스킬 효과 카운터에는 자동 효과가 포함될 수 있으며 명령 대비 성공률로 해석하지 않는다.",
             "기록 시각은 저장된 산출물의 수정 시각이다. 이 파일은 채팅 자동 알림을 예약하지 않는다.", ""]
    for event in sorted(events, key=lambda e: e["time"]):
        lines.extend([f"- **{event['time']}** — {event['text']}", ""])
    atomic_write(directory / "cohort_summary.md", "\n".join(lines))


def notify_windows(directory, event):
    """Use a transient tray notification; no dialog, focus change, or trainer I/O."""
    if os.name != "nt":
        raise RuntimeError("Windows notifications require Windows")
    if event["kind"] == "cohort":
        title = f"강화학습: {NAMES[event['race']]} 코호트 {event.get('cohort_number', event['version'])} 완료"
        body = (f"{event['games']}경기 {event['wins']}승 {event['losses']}패 / 미결 {event['truncated']}\n"
                f"가중치 v{event['version']} 저장 완료\n"
                f"스킬 명령 {sum(event['skills']['ability_orders'])}회, 변신 {event['morph_orders']}회\n"
                f"금지 행동 {event['mask_violations']}건 / 명령 거절 {event['silent_rejections']}건")
    else:
        title, body = "강화학습 보고", event["text"]
    payload = directory / "notification_payload.json"
    atomic_write(payload, json.dumps(dict(title=title, body=body), ensure_ascii=False))
    script = r"""
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
$payload = [System.IO.File]::ReadAllText($env:RANKER_COHORT_NOTIFICATION_FILE, [System.Text.Encoding]::UTF8) | ConvertFrom-Json
$icon = New-Object System.Windows.Forms.NotifyIcon
$script:balloonShown = $false
try {
    $icon.add_BalloonTipShown({ $script:balloonShown = $true })
    $icon.Icon = [System.Drawing.SystemIcons]::Information
    $icon.Text = 'Ranker RL'
    $icon.BalloonTipTitle = $payload.title
    $icon.BalloonTipText = $payload.body
    $icon.Visible = $true
    $icon.ShowBalloonTip(12000)
    $deadline = [DateTime]::UtcNow.AddSeconds(13)
    while ([DateTime]::UtcNow -lt $deadline) {
        [System.Windows.Forms.Application]::DoEvents()
        Start-Sleep -Milliseconds 100
    }
} finally {
    $icon.Dispose()
}
@{ displayed = $script:balloonShown } | ConvertTo-Json -Compress
"""
    environment = dict(os.environ, RANKER_COHORT_NOTIFICATION_FILE=str(payload.resolve()))
    result = subprocess.run(["powershell.exe", "-NoProfile", "-NonInteractive", "-WindowStyle", "Hidden",
                             "-EncodedCommand", base64.b64encode(script.encode("utf-16-le")).decode("ascii")],
                            env=environment, capture_output=True, timeout=25,
                            creationflags=subprocess.CREATE_NO_WINDOW)
    if result.returncode:
        raise RuntimeError(result.stderr.decode(errors="replace")[-1000:])
    return json.loads(result.stdout)


def run(args):
    directory = args.work_dir / "reports"
    directory.mkdir(exist_ok=True)
    # A separate lock avoids touching the trainer's own lock or STOP sentinel.
    lock = (directory / "observer.lock").open("a+b")
    if os.name == "nt":
        import msvcrt
        if lock.tell() == 0:
            lock.write(b"0")
            lock.flush()
        lock.seek(0)
        msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
    try:
        events_path = directory / "events.json"
        events = read_json(events_path) if events_path.exists() else []
        notify = getattr(args, "notify_windows", False)
        # On first attachment, write history but do not display a notification storm.
        can_notify = bool(events)
        notification_error = None
        while True:
            state = read_json(args.work_dir / "state.json")
            new = scan(args.work_dir, events, state)
            if new or not events_path.exists():
                events.extend(new)
                save_reports(directory, events)
            for event in new:
                print(json.dumps(event, ensure_ascii=False), flush=True)
                if notify and can_notify:
                    try:
                        delivery = notify_windows(directory, event)
                        notification_error = None
                        receipt = dict(event_id=event["id"], time=stamp(), api_result="submitted", **delivery)
                    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
                        notification_error = str(error)
                        receipt = dict(event_id=event["id"], time=stamp(), api_result="failed", error=str(error))
                    with (directory / "notifications.jsonl").open("a", encoding="utf-8") as stream:
                        stream.write(json.dumps(receipt, ensure_ascii=False) + "\n")
            can_notify = True
            current = progress(args.work_dir, state)
            current.update(observer_pid=os.getpid(), event_count=len(events), error=None,
                           windows_notifications=notify, notification_error=notification_error)
            atomic_write(directory / "observer.json", json.dumps(current, ensure_ascii=False, indent=2) + "\n")
            if not args.watch or state["status"] in TERMINAL or (directory / "STOP").exists():
                return
            time.sleep(args.poll_seconds)
    finally:
        lock.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work-dir", type=Path, default=DEFAULT_WORK)
    parser.add_argument("--watch", action="store_true")
    parser.add_argument("--notify-windows", action="store_true", help="Show new reports as Windows tray notifications")
    parser.add_argument("--poll-seconds", type=float, default=5)
    args = parser.parse_args()
    if args.poll_seconds <= 0:
        parser.error("--poll-seconds must be positive")
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    try:
        run(args)
    except Exception as error:
        directory = args.work_dir / "reports"
        if directory.is_dir():
            atomic_write(directory / "observer_error.json", json.dumps(
                dict(time=stamp(), observer_pid=os.getpid(), error=f"{type(error).__name__}: {error}"),
                ensure_ascii=False, indent=2) + "\n")
        raise


if __name__ == "__main__":
    main()
