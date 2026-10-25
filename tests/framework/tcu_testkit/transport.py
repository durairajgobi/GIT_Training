# SPDX-License-Identifier: MIT
"""Command transports for lab equipment (SSH to DUT, head unit, APs, sniffers)."""
from __future__ import annotations

import shlex
import subprocess
from dataclasses import dataclass


@dataclass
class CommandResult:
    rc: int
    stdout: str
    stderr: str

    @property
    def ok(self) -> bool:
        return self.rc == 0

    def check(self) -> "CommandResult":
        if self.rc != 0:
            raise RuntimeError(f"command failed rc={self.rc}: {self.stderr.strip()}")
        return self


class Transport:
    name = "transport"

    def run(self, cmd: str, timeout: float = 30) -> CommandResult:  # pragma: no cover
        raise NotImplementedError


class LocalTransport(Transport):
    name = "local"

    def run(self, cmd: str, timeout: float = 30) -> CommandResult:
        p = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=timeout)
        return CommandResult(p.returncode, p.stdout, p.stderr)


class SSHTransport(Transport):
    """OpenSSH client with connection multiplexing so 300 cases don't pay
    a handshake each; BatchMode makes a missing key fail instead of hang."""

    def __init__(self, host: str, user: str = "root", port: int = 22,
                 key: str | None = None, jump: str | None = None):
        self.name = f"{user}@{host}"
        self.base = [
            "ssh", "-p", str(port),
            "-o", "BatchMode=yes",
            "-o", "StrictHostKeyChecking=accept-new",
            "-o", "ConnectTimeout=10",
            "-o", "ControlMaster=auto",
            "-o", f"ControlPath=/tmp/tcu-testkit-%r@%h:%p",
            "-o", "ControlPersist=300",
        ]
        if key:
            self.base += ["-i", key]
        if jump:
            self.base += ["-J", jump]
        self.base.append(f"{user}@{host}")

    def run(self, cmd: str, timeout: float = 30) -> CommandResult:
        try:
            p = subprocess.run([*self.base, cmd], capture_output=True, text=True, timeout=timeout)
        except subprocess.TimeoutExpired:
            return CommandResult(124, "", f"timeout after {timeout}s: {cmd}")
        return CommandResult(p.returncode, p.stdout, p.stderr)


def q(value: str) -> str:
    """Quote a value for the remote shell."""
    return shlex.quote(value)
