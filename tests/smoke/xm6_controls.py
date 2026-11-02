"""Read XM6 state, optionally exercise controls and restore confirmed settings."""
import argparse
import json
from pathlib import Path
import queue
import re
import subprocess
import threading


def redact(message):
    return re.sub(r"(?i)(?:[0-9a-f]{2}:){5}[0-9a-f]{2}", "<REDACTED>", message)


class Session:
    def __init__(self, executable):
        self.process = subprocess.Popen(
            [str(Path(executable).resolve()), "--session"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
            text=True, encoding="utf-8",
        )
        self.responses = queue.Queue()
        self.request_id = 0

        def read():
            for line in self.process.stdout:
                try:
                    message = json.loads(line)
                    if "id" in message:
                        self.responses.put(message)
                except json.JSONDecodeError:
                    pass
        threading.Thread(target=read, daemon=True).start()

    def command(self, command):
        self.request_id += 1
        self.process.stdin.write(json.dumps({"id": self.request_id, "command": command}) + "\n")
        self.process.stdin.flush()
        response = self.responses.get(timeout=25)
        assert response["id"] == self.request_id
        assert response["success"], redact(f"{command}: {response['message']}")
        return json.loads(response["data"])

    def close(self):
        self.process.stdin.close()
        try:
            self.process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("executable")
    parser.add_argument("--exercise", action="store_true")
    args = parser.parse_args()
    session = Session(args.executable)
    try:
        state = session.command("info")
        assert state["name"] == "WH-1000XM6", "Hardware validation requires WH-1000XM6"
        if not args.exercise:
            state = session.command("refresh settings")
        selected = {key: state[key] for key in (
            "name", "protocol", "firmware", "codec", "noiseControl",
            "autoPowerOff", "autoPowerOffOptions",
        )}
        selected["features"] = {key: state["features"].get(key) for key in (
            "noiseControl", "autoPowerOff", "firmware",
        )}
        print(json.dumps(selected, ensure_ascii=True, indent=2), flush=True)
        if args.exercise:
            original_noise = state["noiseControl"]
            original_power = state["autoPowerOff"]
            assert state["features"]["noiseControl"]["availability"] == "valid"
            assert state["features"]["autoPowerOff"]["availability"] == "valid"
            assert not original_noise["focusOnVoice"], "Voice focus is enabled; leave it unchanged"
            try:
                for command, expected in (("anc on", "cancelling"), ("ambient 10", "ambient"), ("anc off", "off")):
                    session.command(command)
                    actual = session.command("refresh noise")["noiseControl"]
                    assert actual["mode"] == expected, (command, actual)
                    print(f"PASS {command}: device readback {expected}", flush=True)
                indexes = state["autoPowerOffOptions"]
                if original_power in (0, 5):
                    # Keep the unexposed remembered timer untouched.
                    indexes = [index for index in indexes if index in (0, 5)]
                for index in indexes:
                    actual = session.command(f"apo {index}")
                    assert actual["autoPowerOff"] == index
                    assert actual["features"]["autoPowerOff"]["availability"] == "valid"
                    print(f"PASS apo {index}: confirmed by device readback", flush=True)
            finally:
                session.command(f"apo {original_power}")
                if original_noise["mode"] == "ambient":
                    session.command(f"ambient {original_noise['ambientLevel']}")
                else:
                    session.command("anc on" if original_noise["mode"] == "cancelling" else "anc off")
                actual = session.command("refresh noise")["noiseControl"]
                assert actual["mode"] == original_noise["mode"]
                print("PASS restored original power and noise modes", flush=True)
    finally:
        session.close()


if __name__ == "__main__":
    main()
