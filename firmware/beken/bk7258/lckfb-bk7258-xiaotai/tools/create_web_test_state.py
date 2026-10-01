#!/usr/bin/env python3
"""Create a mode-0600 Playwright storage state without logging credentials."""

import argparse
import getpass
import json
import os
import urllib.error
import urllib.request
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-url", default="https://xiaotai.chat")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    email = input("Email: ").strip()
    password = getpass.getpass("Password: ")
    payload = json.dumps({
        "email": email,
        "password": password,
        "captcha_id": "",
        "validate": "",
        "user": "",
        "captcha": None,
    }).encode()
    request = urllib.request.Request(
        args.base_url.rstrip("/") + "/v1/user/login",
        data=payload,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(request, timeout=20) as response:
            body = json.load(response)
    except urllib.error.HTTPError as error:
        body = json.load(error)
    token = body.get("data", {}).get("token")
    if body.get("code") != 200 or not token:
        raise SystemExit(f"login failed code={body.get('code')} msg={body.get('msg', '')}")
    state = {
        "cookies": [],
        "origins": [{
            "origin": args.base_url.rstrip("/"),
            "localStorage": [{"name": "token", "value": token}],
        }],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    descriptor = os.open(args.output, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(descriptor, "w", encoding="utf-8") as state_file:
        json.dump(state, state_file)
    print(f"storage state created: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
