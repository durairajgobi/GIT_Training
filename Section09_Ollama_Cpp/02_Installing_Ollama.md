# Lesson 2 – Installing and configuring Ollama

Check <https://ollama.com/download> for the current minimum OS versions and
hardware support; they change between releases.

After installing on **any** platform, verify the server:

```bash
ollama --version
```

```bash
curl http://localhost:11434
```

The second command should print `Ollama is running`.

---

## Windows

1. Download **OllamaSetup.exe** from <https://ollama.com/download/windows>
   and run it. No admin rights are needed (it installs per-user).
2. Ollama starts automatically and lives in the system tray. The server runs
   in the background whenever the tray app is running.
3. Open a new **PowerShell** or **Command Prompt** and test:
   ```powershell
   ollama run llama3.2
   ```

**Configuration** is done with environment variables:

1. Quit Ollama from the tray icon.
2. Open *Settings → System → About → Advanced system settings →
   Environment Variables* (or search "environment variables").
3. Add or edit a **user** variable, for example:
   - `OLLAMA_MODELS` = `D:\ollama\models` (store models on another drive)
   - `OLLAMA_HOST` = `0.0.0.0:11434` (allow other machines on your LAN)
4. Start Ollama again from the Start menu.

| Item | Location |
|------|----------|
| Models | `%USERPROFILE%\.ollama\models` |
| Logs | `%LOCALAPPDATA%\Ollama` (`server.log`) |

**GPU**: NVIDIA works with a current driver. Recent AMD Radeon cards are
supported too; see Ollama's GPU docs.

**C++ toolchain**: Visual Studio 2022 (Desktop development with C++), CMake
and [vcpkg](https://vcpkg.io):

```powershell
vcpkg install curl nlohmann-json
```

```powershell
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg-root>/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

---

## macOS

Option A, the app (recommended):

1. Download **Ollama for macOS** from <https://ollama.com/download/mac>.
2. Unzip it, drag **Ollama.app** into *Applications* and open it. It asks to
   install the `ollama` command-line tool; allow it.
3. A llama icon in the menu bar means the server is running.

Option B, Homebrew:

```bash
brew install ollama
```

```bash
brew services start ollama
```

(or run `ollama serve` in a terminal you keep open).

**Configuration** for the app uses `launchctl`, then restart Ollama:

```bash
launchctl setenv OLLAMA_HOST "0.0.0.0:11434"
```

| Item | Location |
|------|----------|
| Models | `~/.ollama/models` |
| Logs | `~/.ollama/logs/server.log` |

**GPU**: Apple Silicon (M1 and later) uses Metal automatically. Unified
memory means a 16 GB Mac can comfortably run 7B–8B models.

**C++ toolchain**:

```bash
xcode-select --install
```

```bash
brew install cmake nlohmann-json
```

(libcurl ships with macOS.)

---

## Ubuntu Linux

Install with the official script, which also creates a `systemd` service and
an `ollama` user:

```bash
curl -fsSL https://ollama.com/install.sh | sh
```

Check the service:

```bash
systemctl status ollama
```

**Configuration** is done with a systemd override:

```bash
sudo systemctl edit ollama
```

Add, for example:

```ini
[Service]
Environment="OLLAMA_HOST=0.0.0.0:11434"
Environment="OLLAMA_MODELS=/data/ollama/models"
```

Then reload and restart:

```bash
sudo systemctl daemon-reload && sudo systemctl restart ollama
```

(If you move `OLLAMA_MODELS`, the directory must be writable by the
`ollama` user: `sudo chown -R ollama:ollama /data/ollama`.)

| Item | Location / command |
|------|-------------------|
| Models | `/usr/share/ollama/.ollama/models` |
| Logs | `journalctl -e -u ollama` |

**GPU**: install the NVIDIA driver (`sudo ubuntu-drivers install`) before
Ollama. For AMD, the install script fetches the ROCm components.

**Update** by re-running the install script. **Uninstall**:

```bash
sudo systemctl disable --now ollama && sudo rm /etc/systemd/system/ollama.service /usr/local/bin/ollama
```

**C++ toolchain**:

```bash
sudo apt update && sudo apt install -y build-essential cmake libcurl4-openssl-dev nlohmann-json3-dev
```

---

## Useful environment variables (all platforms)

| Variable | Purpose | Default |
|----------|---------|---------|
| `OLLAMA_HOST` | Address the server binds to (and the CLI connects to) | `127.0.0.1:11434` |
| `OLLAMA_MODELS` | Where model files are stored | per-OS path above |
| `OLLAMA_KEEP_ALIVE` | How long a model stays loaded after the last request | `5m` |
| `OLLAMA_CONTEXT_LENGTH` | Default context window for all models | a few thousand tokens |
| `OLLAMA_ORIGINS` | Extra allowed browser origins (CORS) | localhost |

> **Security:** binding to `0.0.0.0` exposes an unauthenticated API to your
> whole network. Only do it on a trusted network, or put a firewall/reverse
> proxy in front.

The C++ program in `code/` reads `OLLAMA_HOST` too, so it follows whatever
you configure here.

## Section 9 quiz – Installing Ollama

1. Which command confirms the server is reachable?
2. On Ubuntu, how do you change Ollama's environment variables?
3. Where are models stored on macOS?
4. What is the risk of setting `OLLAMA_HOST=0.0.0.0`?

<details><summary>Answers</summary>

1. `curl http://localhost:11434` prints "Ollama is running". `ollama --version` checks the CLI.
2. `sudo systemctl edit ollama`, add `Environment="..."` under `[Service]`, then `daemon-reload` and `restart`.
3. `~/.ollama/models`.
4. Anyone on the network can use the API, which has no authentication.
</details>
