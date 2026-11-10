#!/usr/bin/env python3
"""Build and optionally POST pairing JSON to the robot's AP HTTP service."""

import argparse
import getpass
import hashlib
import json
import sys
import time
import re
from pathlib import Path
import urllib.error
import urllib.parse
import urllib.request


def check_url(value: str, label: str) -> str:
    parsed = urllib.parse.urlsplit(value)
    if parsed.scheme not in {"http", "https"} or not parsed.hostname:
        raise ValueError(f"{label} must be an absolute HTTP(S) URL")
    if parsed.username or parsed.password or parsed.fragment:
        raise ValueError(f"{label} must not contain credentials or a fragment")
    if not re.fullmatch(r"https?://[A-Za-z0-9.:-]+/[A-Za-z0-9_./-]*", value):
        raise ValueError(f"{label} contains characters unsafe for the vendor shell downloader")
    if parsed.query:
        raise ValueError(f"{label} must not contain a query")
    return value


def build_config(
    ssid: str, password: str, app_type: str, region_info: str,
    register_url: str, script_url: str, script_md5: str,
) -> dict:
    return {
        # Original WeBack rejects a missing Version silently, before Wi-Fi parsing.
        "Version": "1.0",
        # Nonempty parser field for local script installation, not a cloud account.
        "Identity_Id": "local",
        "timestamp_s": int(time.time()),
        "WiFi_Name": ssid,
        "WiFi_Password": password,
        "App_Type": app_type,
        "Region_Info": region_info,
        "Register_Url": register_url,
        "script_url": script_url,
        "script_md5": script_md5,
    }


def check_response(body: bytes) -> None:
    """Do not print vendor bodies: they may include credentials/identifiers."""
    try:
        result = json.loads(body)
    except (ValueError, UnicodeError) as exc:
        raise ValueError('Robot returned an invalid JSON response; check installation before retrying') from exc
    if not isinstance(result, dict):
        raise ValueError('Robot returned an unexpected response; check installation before retrying')
    if 'Result' in result:
        if result['Result'] not in (True, 'true'):
            raise ValueError('Robot rejected the pairing request (Result is not true)')
    else:
        raise ValueError('Robot response does not confirm pairing acceptance (missing Result)')


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ssid", required=True, help="SSID of the Wi-Fi the robot must join")
    parser.add_argument("--app-type", required=True, help="App_Type for this robot (Redmond in the studied unit)")
    parser.add_argument("--region-info", default="0086", help="Region_Info (default from firmware: 0086)")
    parser.add_argument(
        "--register-url", default="http://127.0.0.1/",
        help="Nonempty placeholder to pass firmware validation; script path skips registration",
    )
    parser.add_argument("--script-url", required=True, help="URL reachable by the robot after joining Wi-Fi")
    parser.add_argument("--script-file", type=Path, help="Exact local copy of this session install.sh; computes MD5 without contacting the server")
    parser.add_argument("--robot-url", default="http://192.168.99.1/", help="Pairing HTTP endpoint")
    parser.add_argument("--timeout", type=float, default=10, help="HTTP timeout in seconds")
    parser.add_argument("--send", action="store_true", help="Actually POST to the robot; otherwise dry-run")
    args = parser.parse_args()

    try:
        script_url = check_url(args.script_url, "script URL")
        robot_url = check_url(args.robot_url, "robot URL")
        check_url(args.register_url, "register URL")
        if any(c in args.ssid for c in '"\\\r\n'):
            raise ValueError("This vendor pairing path cannot safely encode quotes, backslashes or newlines in SSIDs")
        if not args.ssid or not args.app_type or not args.region_info:
            raise ValueError("SSID, App_Type, and Region_Info must be nonempty")

        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        if args.script_file is not None:
            with args.script_file.open('rb') as source:
                script = source.read(1024 * 1024 + 1)
            print('Using local script copy; installer server not contacted. '
                  'The file and URL must belong to the same generated session.')
        else:
            with opener.open(script_url, timeout=args.timeout) as response:
                script = response.read(1024 * 1024 + 1)
        if len(script) > 1024 * 1024:
            raise ValueError("Installer script exceeds 1 MiB")
        if not script:
            raise ValueError("Installer script is empty")
        digest = hashlib.md5(script).hexdigest()
        password = getpass.getpass("Wi-Fi password (not stored on this PC): ", stream=sys.stderr)
        if any(c in password for c in '"\\\r\n'):
            raise ValueError("This vendor pairing path cannot safely encode quotes, backslashes or newlines in passwords")
        config = build_config(
            args.ssid, password, args.app_type, args.region_info,
            args.register_url, script_url, digest,
        )
        displayed = {**config, "WiFi_Password": "<hidden>"}
        print(json.dumps(displayed, ensure_ascii=False, indent=2))
        if not args.send:
            print("Dry-run only. Add --send to POST this JSON to the robot.")
            return 0

        body = json.dumps(config, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        request = urllib.request.Request(
            robot_url,
            data=body,
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        with opener.open(request, timeout=args.timeout) as response:
            answer = response.read(4097)
            if len(answer) > 4096:
                raise ValueError("Robot response exceeds the expected size")
            check_response(answer)
            print(f"Robot accepted pairing (HTTP {response.status}); installation still needs verification.")
            print("Response body omitted to avoid exposing vendor-provided credentials.")
        print("Check the installer server log for GET /install.sh, then test SSH.")
        return 0
    except (OSError, ValueError, urllib.error.URLError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        if args.send:
            print("The POST may already have reached the robot; check the server log before retrying.", file=sys.stderr)
        return 1


if __name__ == "__main__":
    from console_output import configure_output
    configure_output()
    raise SystemExit(main())
