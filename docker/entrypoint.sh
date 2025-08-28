#!/usr/bin/env bash
set -Eeuo pipefail

# Configure & build
mkdir -p /workspace/build
cmake -S /workspace -B /workspace/build -DCMAKE_BUILD_TYPE="${BUILD_TYPE:-RelWithDebInfo}"
cmake --build /workspace/build -j"$(nproc)"

# Use repo's data/ as working directory
if [[ ! -d /workspace/data ]]; then
  echo "ERROR: /workspace/data is missing. Create it in your repo." >&2
  exit 1
fi
cd /workspace/data

# Token must exist
if [[ ! -s .bot_token ]]; then
  echo "ERROR: missing /workspace/data/.bot_token" >&2
  exit 1
fi

# Ensure JSON files exist and are non-empty JSON arrays
[[ -f users.json   ]] || printf '[]' > users.json
[[ -s users.json   ]] || printf '[]' > users.json
[[ -f matches.json ]] || printf '[]' > matches.json
[[ -s matches.json ]] || printf '[]' > matches.json

# Line-buffer so docker logs shows output immediately
exec stdbuf -oL -eL /workspace/build/bin/terry_aoe2_dcbot
