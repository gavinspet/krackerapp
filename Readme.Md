# Project Overview

**Goal:** Build a production-grade private chatting web application you can showcase in your portfolio. Modern C++ backend with clean API design, real-time messaging, accounts/auth, and polished UI. Deploy to the internet with TLS so friends can sign up and chat.

**Stack (battle-tested & hire-friendly):**

* **Backend:** C++20 with **Drogon** (REST + WebSocket), **CMake**.
* **Auth & Security:** **JWT (jwt-cpp)** access tokens + refresh tokens; **Argon2id** password hashing; rate limiting; input validation.
* **Database:** **PostgreSQL** (via **libpqxx**). Optional: **Redis** for caching/presence later.
* **Realtime:** WebSocket endpoint (rooms/DMs, typing, read receipts).
* **Frontend:** **React + Vite + TypeScript + Tailwind + shadcn/ui** (sleek, modern, easy to style).
* **Docs:** **OpenAPI 3.0** (served with Swagger UI).
* **Observability:** structured logs (spdlog), health checks.
* **Infra:** Docker (dev & prod), **Caddy** reverse-proxy (TLS), **Fly.io** (deploy). Alternatives: Render/Railway.
* **CI/CD:** GitHub Actions.

---

# Repository Layout

```
chat-app/
  server/              # C++ backend (Drogon)
    CMakeLists.txt
    src/
      main.cpp
      controllers/
        AuthController.h/.cc
        MessageController.h/.cc
        ConversationController.h/.cc
        WebsocketHub.h/.cc
      models/          # (generated via drogon_ctl) Users, Conversations, Messages, Participants, RefreshTokens
      filters/
        AuthRequired.h/.cc
        RateLimit.h/.cc
      utils/
        Jwt.h/.cc
        Crypto.h/.cc
      config.json
    include/
    tests/
  web/                 # React + Vite + TS
    index.html
    src/
      App.tsx
      pages/
      components/
      lib/api.ts
  ops/
    schema.sql         # migrations (v1)
    seed.sql
  deploy/
    Dockerfile.server
    Dockerfile.web
    docker-compose.dev.yml
    Caddyfile
    fly.toml
  README.md
```

---

# Data Model

**Tables** (UUID PKs, created\_at timestamptz default now()):

* **users**: id, username (unique), email (unique, nullable), password\_hash, last\_seen, avatar\_url.
* **conversations**: id, is\_group (bool), title (nullable), created\_by.
* **conversation\_participants**: conversation\_id, user\_id, role ("owner"|"member"), joined\_at.
* **messages**: id, conversation\_id, sender\_id, content (text), created\_at, edited\_at (nullable), deleted (bool default false).
* **refresh\_tokens**: id, user\_id, token\_hash, expires\_at, revoked (bool).

**Indexes:** users(username), users(email), messages(conversation\_id, created\_at), conversation\_participants(user\_id), refresh\_tokens(user\_id, token\_hash).

---

# API Surface (v1)

**Auth**

* `POST /api/v1/auth/register` {username, email?, password}
* `POST /api/v1/auth/login` {username\_or\_email, password} → {accessToken (JWT, \~15m), refreshToken (httpOnly cookie or body)}
* `POST /api/v1/auth/refresh` → new accessToken
* `POST /api/v1/auth/logout` (revokes refresh token)

**Conversations**

* `GET /api/v1/conversations` (list user’s DMs & groups)
* `POST /api/v1/conversations` {participantIds\[], title?, isGroup?}
* `GET /api/v1/conversations/:id`

**Messages**

* `GET /api/v1/conversations/:id/messages?before&limit`
* `POST /api/v1/conversations/:id/messages` {content}
* `DELETE /api/v1/messages/:id` (soft delete)

**Realtime (WebSocket)** `/ws`

* Client connects with `Authorization: Bearer <accessToken>` in query/header.
* Message envelope (JSON): `{type, conversationId?, payload}`

  * Types: `join`, `leave`, `message`, `typing_start`, `typing_stop`, `read`, `presence`.
* Server broadcasts to participants, persists `message` events, emits `ack`.

**OpenAPI**: `openapi.yaml` (served at `/api/docs` with Swagger UI).

---

# Security & Auth

* **Passwords:** Argon2id (salted, memory-hard).
* **JWT:** HS256/EdDSA. Short-lived **access token** (15–30m). **Refresh token** (\~7–30d) stored httpOnly (or DB hashed) + rotation on refresh.
* **Rate limiting:** simple leaky-bucket per-IP on auth routes.
* **Validation:** reject oversized payloads; strict JSON schema; SQL params bound via libpqxx (no string concat).
* **Transport:** TLS via Caddy in prod; SameSite/secure cookies.

---

# Step-by-Step Build Guide

## 0) Prereqs (Ubuntu)

```bash
sudo apt update && sudo apt install -y build-essential cmake git pkg-config \
  openssl libssl-dev zlib1g-dev libargon2-1 libargon2-dev \
  postgresql postgresql-contrib libpq-dev
# Optional: vcpkg for dependencies
git clone https://github.com/microsoft/vcpkg.git $HOME/vcpkg
$HOME/vcpkg/bootstrap-vcpkg.sh
```

## 1) Create the Drogon project

```bash
mkdir -p ~/code/chat-app && cd ~/code/chat-app
mkdir server && cd server
# Install Drogon (choose one)
# A) via package manager (Ubuntu 24.04+ may have drogon)
# sudo apt install -y drogon libdrogon-dev
# B) via vcpkg
$HOME/vcpkg/vcpkg install drogon
# C) from source (fast path)
# git clone https://github.com/drogonframework/drogon && cd drogon && mkdir build && cd build
# cmake .. -DCMAKE_BUILD_TYPE=Release && make -j && sudo make install

# Scaffold project files
mkdir -p src/controllers src/models src/filters include utils
```

## 2) Add CMakeLists.txt (server/CMakeLists.txt)

```cmake
cmake_minimum_required(VERSION 3.16)
project(chat_server LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)

find_package(Drogon REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBPQ REQUIRED libpq)

# jwt-cpp is header-only; add as submodule or vcpkg
# vcpkg: $ENV{VCPKG_ROOT}/vcpkg install jwt-cpp
# Argon2 (system)
find_path(ARGON2_INCLUDE_DIR argon2.h)
find_library(ARGON2_LIB argon2)

file(GLOB SRC src/**/*.cc src/*.cpp)
add_executable(chat_server ${SRC})

# include
include_directories(include ${ARGON2_INCLUDE_DIR})

# link
target_link_libraries(chat_server PRIVATE Drogon::Drogon ${ARGON2_LIB} pq)

install(TARGETS chat_server RUNTIME DESTINATION bin)
```

## 3) Minimal `main.cpp`

```cpp
#include <drogon/drogon.h>
int main() {
  using namespace drogon;
  app().addListener("0.0.0.0", 8080);
  app().setLogPath("./logs").setLogLevel(trantor::Logger::kInfo).enableSession(false);
  app().loadConfigFile("config.json");
  app().run();
}
```

## 4) `config.json` (DB + threading)

```json
{
  "listeners": [{"address":"0.0.0.0","port":8080}],
  "app": {"threads_num": 0},
  "db_clients": [{
    "name": "default",
    "rdbms": "postgresql",
    "host": "127.0.0.1",
    "port": 5432,
    "dbname": "chatdb",
    "user": "chat",
    "password": "chatpw",
    "is_fast": true,
    "number_of_connections": 5
  }]
}
```

## 5) SQL schema (ops/schema.sql)

```sql
create extension if not exists "uuid-ossp";
create table if not exists users (
  id uuid primary key default gen_random_uuid(),
  username text unique not null,
  email text unique,
  password_hash text not null,
  last_seen timestamptz default now(),
  avatar_url text,
  created_at timestamptz default now()
);
create table if not exists conversations (
  id uuid primary key default gen_random_uuid(),
  is_group boolean not null default false,
  title text,
  created_by uuid references users(id) on delete set null,
  created_at timestamptz default now()
);
create table if not exists conversation_participants (
  conversation_id uuid references convers
```
