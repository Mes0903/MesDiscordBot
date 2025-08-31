# Terry Discord Team Manager Bot

A lightweight Discord bot for **team management** and **match tracking** inside a server. It keeps a registry of users with a combat power value, lets you form **balanced yet randomized** teams, and records match results with a simple history view.

Built in **C++23**, using **CMake** and **[DPP](https://dpp.dev/)**.

PR and issues are welcome.

## Repository Layout

```
mes@mes:~/MesRepo/terry-aoe2-DCbot$ tree -aL 3
.
├── 3rdparty
│   └── json
...
├── build
...
├── .clang-format
├── cmake
│   └── FindDPP.cmake
├── CMakeLists.txt
├── data
│   ├── .bot_token
│   ├── matches.json
│   └── users.json
├── docker
│   ├── docker-compose.yml
│   ├── DOCKERFILE
│   ├── .dockerignore
│   └── entrypoint.sh
├── .git
...
├── .gitignore
├── .gitmodules
├── include
│   ├── core
│   │   ├── constants.hpp
│   │   └── utils.hpp
│   ├── handlers
│   │   ├── command_handler.hpp
│   │   ├── interaction_handler.hpp
│   │   └── session_manager.hpp
│   ├── models
│   │   ├── match.hpp
│   │   ├── team.hpp
│   │   └── user.hpp
│   ├── services
│   │   ├── match_service.hpp
│   │   ├── persistence_service.hpp
│   │   └── team_service.hpp
│   └── ui
│       ├── embed_builder.hpp
│       ├── message_builder.hpp
│       └── panel_builder.hpp
├── Makefile
├── README.md
└── src
    ├── handlers
    │   ├── command_handler.cpp
    │   ├── interaction_handler.cpp
    │   └── session_manager.cpp
    ├── main.cpp
    ├── services
    │   ├── match_service.cpp
    │   ├── persistence_service.cpp
    │   └── team_service.cpp
    └── ui
        ├── embed_builder.cpp
        └── panel_builder.cpp
```

> **Note**: Persistent data lives in `data/` (JSON files). Do **not** commit the bot token.

## Requirements

- A C++23-capable compiler (e.g., GCC 14+ / Clang 16+)
- CMake 3.22+
- [DPP](https://dpp.dev/) (Discord++ library) discoverable by CMake (see `cmake/FindDPP.cmake`).
  - See [Installing D++](https://dpp.dev/installing.html) for more detail.
- A Discord bot token stored in **`data/.bot_token`** (file with a single line: the token string).
  - You need to make the `data` folder manually.

## Build & Run (native)

> The project was only tested on Ubuntu24.04 for now, but I think it's cross-platform since there is no platform-dependent code.

To build the project:

```bash
# from repo root
git submodule update --init --recursive  # ensure 3rdparty/json is present
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
```

To run the bot:

```bash
cmake --build build -t run
```

For convenience, a `Makefile` is also provided. You can compile the project directly using the `make` command, with the default configuration set to Release. Also using the `make run` command to start the bot. For more details, please refer to the contents of the `Makefile`.

> **Important**: put your bot token into `data/.bot_token` before running. `users.json` and `matches.json` will be created on first run if missing.

## Run with Docker

### 1. One-time setup

- Place your bot token in `data/.bot_token`.
- Make sure `data/users.json` and `data/matches.json` exist (empty `[]` is fine). The entrypoint will create them if absent.

### 2. Build & start

```bash
cd docker
# Build the image and start the container
# (uses repo root as /workspace and mounts ./data to container runtime dir)
docker compose up --build
```

The container will compile the project and start the bot. Logs stream in your terminal. Use `Ctrl+C` to stop, or run with `-d` for detached mode.

> The compose file mounts the repository into `/workspace` and maps your host `data/` to the container’s runtime directory. The `entrypoint.sh` ensures the token and JSON files exist and then launches the bot.

## Token & Data

- **Token**: `data/.bot_token` (one line, no quotes). Never commit it.
- **Users**: `data/users.json` (list of objects with `id`, `username`, `combat_power`, plus stats fields).
- **Matches**: `data/matches.json` (append-only history with timestamp, winning team indexes, and member IDs).

> These files are read on startup and saved on clean shutdown. They are small, human-readable JSON for easy backups.

## Commands (Slash)

| Command       | Arguments                          | Purpose                                                                                        |
| ------------- | ---------------------------------- | ---------------------------------------------------------------------------------------------- |
| `/adduser`    | `user` (mention), `point` (number) | Add or update a user’s combat power.                                                           |
| `/removeuser` | `user` (mention)                   | Remove a user from the registry.                                                               |
| `/listusers`  | —                                  | List all registered users.                                                                     |
| `/formteams`  | `teams` (int, default 2)           | Open the team-formation panel. Select participants and press Assign to generate teams.         |
| `/history`    | `count` (int, default 5)           | Show recent matches, including winners and team compositions.                                  |
| `/sethistory`  | —                                  | Open the winner-setting panel for the most recent matches (select a match, then mark winners). |
| `/help`       | —                                  | Show an embedded help panel summarizing all commands.                                          |

