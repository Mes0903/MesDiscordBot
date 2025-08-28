#!/usr/bin/env bash
set -Eeuo pipefail

# Configure (default Unix Makefiles)
mkdir -p /workspace/build
cmake -S /workspace -B /workspace/build -DCMAKE_BUILD_TYPE="${BUILD_TYPE:-RelWithDebInfo}"

# Build (incremental)
cmake --build /workspace/build -j"$(nproc)"

# Runtime files
mkdir -p /workspace/run
cd /workspace/run
if [[ ! -s .bot_token ]]; then
  echo "ERROR: missing /workspace/run/.bot_token" >&2
  exit 1
fi
[[ -f users.json   ]] || printf '[]' > users.json
[[ -f matches.json ]] || printf '[]' > matches.json

# Run
exec stdbuf -oL -eL /workspace/build/bin/terry_aoe2_dcbot
