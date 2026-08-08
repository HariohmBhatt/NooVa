#!/usr/bin/env python3
"""Generate the public Caddy root CA header used by production firmware."""

from argparse import ArgumentParser
from pathlib import Path


def main() -> None:
    parser = ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    certificate = args.input.read_text(encoding="utf-8").strip()
    if not certificate.startswith("-----BEGIN CERTIFICATE-----"):
        raise SystemExit("input is not a PEM certificate")
    delimiter = "NOVA_CA"
    while f"){delimiter}" in certificate:
        delimiter += "_CA"
    content = (
        "#pragma once\n\n"
        "namespace nova {\n\n"
        f'inline constexpr char kHubRootCa[] = R"{delimiter}({certificate}){delimiter}";\n\n'
        "}  // namespace nova\n"
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(content, encoding="utf-8")


if __name__ == "__main__":
    main()
