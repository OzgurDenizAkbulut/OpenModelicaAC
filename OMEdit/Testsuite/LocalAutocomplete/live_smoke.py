"""Opt-in local runtime smoke test with synthetic Modelica, not a quality benchmark."""

import argparse
import ipaddress
import json
import time
import urllib.error
import urllib.parse
import urllib.request


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--endpoint", required=True, help="Loopback server base URL ending in /v1")
    parser.add_argument("--model", required=True, help="Exact locally hosted model identifier")
    args = parser.parse_args()
    url = urllib.parse.urlsplit(args.endpoint)
    host = url.hostname
    if host == "localhost":
        host = "127.0.0.1"
    try:
        local = ipaddress.ip_address(host).is_loopback
    except ValueError:
        local = False
    if not local or url.scheme not in ("http", "https") or url.username is not None or url.password is not None or url.query or url.fragment:
        parser.error("Use an HTTP(S) loopback URL without credentials, query, or fragment")
    host_port = f"[{host}]" if ":" in host else host
    if url.port is not None:
        host_port += f":{url.port}"
    endpoint = urllib.parse.urlunsplit((url.scheme, host_port, url.path.rstrip("/") + "/completions", "", ""))
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect())
    cases = [
        ("declaration", "model Example\n  parameter Real ", " = 1;\nend Example;"),
        ("equation", "model Decay\n  Real x(start=1);\nequation\n  der(x) = ", ";\nend Decay;"),
        ("multiline", "model TwoStates\n  Real x(start=1);\n  Real y(start=0);\nequation\n  ", "\nend TwoStates;"),
    ]
    results = []
    for name, prefix, suffix in cases:
        payload = {
            "model": args.model,
            "prompt": f"<|fim_prefix|>{prefix}<|fim_suffix|>{suffix}<|fim_middle|>",
            "stream": False,
            "temperature": 0,
            "max_tokens": 128,
            "stop": ["<|fim_prefix|>", "<|fim_suffix|>", "<|fim_middle|>", "<|endoftext|>"],
        }
        request = urllib.request.Request(endpoint, data=json.dumps(payload).encode(), headers={"Content-Type": "application/json"})
        start = time.monotonic()
        result = {"case": name}
        try:
            with opener.open(request, timeout=5) as response:
                raw = response.read(65537)
                if len(raw) > 65536:
                    raise ValueError("Response exceeds 64 KiB")
                data = json.loads(raw)
            choice = data["choices"][0]
            result.update(text=choice["text"], finish_reason=choice.get("finish_reason"), usage=data.get("usage"))
        except (OSError, ValueError, KeyError, IndexError) as error:
            result["error"] = str(error)
        result["elapsed_ms"] = round((time.monotonic() - start) * 1000)
        results.append(result)
    print(json.dumps({"endpoint": args.endpoint, "model": args.model, "cases": results}, indent=2))
    return 1 if any("error" in case for case in results) else 0


if __name__ == "__main__":
    raise SystemExit(main())
