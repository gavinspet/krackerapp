#include <drogon/drogon.h>
#include <json/json.h>
#include <filesystem>
#include <sstream>
#include <string>
#include <stdexcept>

#include <libpq-fe.h>           // libpq for PostgreSQL
#include "utils/Jwt.h"          // makeAccessTokenHS256 / verifyAccessTokenHS256
#include "utils/Crypto.h"       // hashPasswordArgon2id / verifyPasswordArgon2id

using namespace drogon;

// ---------- helpers ----------
static bool loadConfigSmart() {
  namespace fs = std::filesystem;
  if (fs::exists("config.json")) { app().loadConfigFile("config.json"); return true; }
  if (fs::exists("server/config.json")) { app().loadConfigFile("server/config.json"); return true; }
  return false;
}

static std::string envOr(const char* k, const char* d) {
  const char* v = std::getenv(k); return v ? v : d;
}

static PGconn* openConn() {
  // For now, a fixed connection string (can move to env/config later)
  const char* conninfo = "host=127.0.0.1 port=5432 dbname=krackerdb user=kracker password=krackerpw";
  PGconn* c = PQconnectdb(conninfo);
  if (PQstatus(c) != CONNECTION_OK) {
    std::string err = PQerrorMessage(c);
    PQfinish(c);
    throw std::runtime_error("DB connect failed: " + err);
  }
  return c;
}

static bool parseJsonBody(const HttpRequestPtr& req, Json::Value* out, std::string* errMsg=nullptr) {
  try {
    Json::CharReaderBuilder b;
    std::string errs;
    std::istringstream iss(std::string(req->getBody().begin(), req->getBody().end()));
    if (!Json::parseFromStream(b, iss, out, &errs)) {
      if (errMsg) *errMsg = errs;
      return false;
    }
    return true;
  } catch (const std::exception& e) {
    if (errMsg) *errMsg = e.what();
    return false;
  }
}

// ---------- main ----------
int main() {
  // Log CWD and load config (if present in either location)
  char cwd[4096]; if (getcwd(cwd, sizeof(cwd))) { LOG_INFO << "CWD: " << cwd; }
  loadConfigSmart();
  app().setLogLevel(trantor::Logger::kInfo);

  // GET /health -> {"ok":true}
  app().registerHandler(
    "/health",
    [](const HttpRequestPtr&, std::function<void (const HttpResponsePtr &)> &&cb) {
      Json::Value j; j["ok"] = true;
      cb(HttpResponse::newHttpJsonResponse(j));
    },
    {Get}
  );

  // GET /db/health -> libpq SELECT 1 (no ORM dependency)
  app().registerHandler(
    "/db/health",
    [](const HttpRequestPtr&, std::function<void (const HttpResponsePtr &)> &&cb) {
      Json::Value j;
      try {
        PGconn* conn = openConn();
        PGresult* res = PQexec(conn, "SELECT 1");
        if (PQresultStatus(res) == PGRES_TUPLES_OK) {
          j["ok"] = true; j["db"] = "up";
        } else {
          j["ok"] = false; j["db"] = "down"; j["error"] = "SELECT 1 failed";
        }
        PQclear(res);
        PQfinish(conn);
      } catch (const std::exception& e) {
        j["ok"] = false; j["db"] = "down"; j["error"] = e.what();
      }
      cb(HttpResponse::newHttpJsonResponse(j));
    },
    {Get}
  );

  // GET /dev/token?sub=...&username=...  (dev helper; remove in prod)
  app().registerHandler(
    "/dev/token",
    [](const HttpRequestPtr& req, std::function<void (const HttpResponsePtr &)> &&cb) {
      auto q = req->getParameters();
      std::string sub = q.count("sub") ? q.at("sub") : "user-1";
      std::string username = q.count("username") ? q.at("username") : "neo";
      std::string secret = envOr("JWT_SECRET", "dev-secret-change-me");
      auto token = makeAccessTokenHS256(sub, username, secret, 60);
      Json::Value j; j["accessToken"] = token; j["sub"] = sub; j["username"] = username;
      cb(HttpResponse::newHttpJsonResponse(j));
    },
    {Get}
  );

  // GET /api/v1/me  (JWT-protected inline)
  app().registerHandler(
    "/api/v1/me",
    [](const HttpRequestPtr& req, std::function<void (const HttpResponsePtr &)> &&cb) {
      const auto auth = req->getHeader("authorization");
      const std::string bearer = "Bearer ";
      if (auth.size() <= bearer.size() || auth.substr(0, bearer.size()) != bearer) {
        Json::Value err; err["error"] = "missing_bearer";
        auto r = HttpResponse::newHttpJsonResponse(err);
        r->setStatusCode(k401Unauthorized);
        return cb(r);
      }
      const auto token = auth.substr(bearer.size());
      std::string sub, username;
      const std::string secret = envOr("JWT_SECRET", "dev-secret-change-me");
      if (!verifyAccessTokenHS256(token, secret, &sub, &username)) {
        Json::Value err; err["error"] = "invalid_token";
        auto r = HttpResponse::newHttpJsonResponse(err);
        r->setStatusCode(k401Unauthorized);
        return cb(r);
      }
      Json::Value j; j["id"] = sub; j["username"] = username;
      cb(HttpResponse::newHttpJsonResponse(j));
    },
    {Get}
  );

  // POST /api/v1/auth/register  {username, email?, password}
  app().registerHandler(
    "/api/v1/auth/register",
    [](const HttpRequestPtr& req, std::function<void (const HttpResponsePtr &)> &&cb) {
      Json::Value in; std::string perr;
      if (!parseJsonBody(req, &in, &perr)) {
        Json::Value err; err["error"] = "invalid_json"; err["detail"] = perr;
        auto r = HttpResponse::newHttpJsonResponse(err);
        r->setStatusCode(k400BadRequest); return cb(r);
      }

      std::string username = in.get("username","").asString();
      std::string email    = in.get("email","").asString();
      std::string password = in.get("password","").asString();
      if (username.size() < 3 || password.size() < 6) {
        Json::Value err; err["error"] = "weak_input";
        auto r = HttpResponse::newHttpJsonResponse(err);
        r->setStatusCode(k400BadRequest); return cb(r);
      }

      try {
        auto hash = hashPasswordArgon2id(password);
        PGconn* conn = openConn();
        const char* params[3] = { username.c_str(), email.c_str(), hash.c_str() };
        PGresult* res = PQexecParams(conn,
          "INSERT INTO users (username, email, password_hash) "
          "VALUES ($1, NULLIF($2,''), $3) "
          "RETURNING id, username",
          3, nullptr, params, nullptr, nullptr, 0);

        if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) != 1) {
          std::string sqlstate = PQresultErrorField(res, PG_DIAG_SQLSTATE) ? PQresultErrorField(res, PG_DIAG_SQLSTATE) : "";
          std::string msg = PQresultErrorMessage(res) ? PQresultErrorMessage(res) : "insert_failed";
          PQclear(res); PQfinish(conn);
          Json::Value j; j["error"]="register_failed"; j["sqlstate"]=sqlstate; j["detail"]=msg;
          auto r = HttpResponse::newHttpJsonResponse(j); r->setStatusCode(k400BadRequest); return cb(r);
        }

        std::string id    = PQgetvalue(res, 0, 0);
        std::string uname = PQgetvalue(res, 0, 1);
        PQclear(res); PQfinish(conn);

        const std::string secret = envOr("JWT_SECRET", "dev-secret-change-me");
        auto token = makeAccessTokenHS256(id, uname, secret, 60);

        Json::Value out; out["id"]=id; out["username"]=uname; out["accessToken"]=token;
        cb(HttpResponse::newHttpJsonResponse(out));
      } catch (const std::exception& e) {
        Json::Value j; j["error"]="register_exception"; j["detail"]=e.what();
        auto r = HttpResponse::newHttpJsonResponse(j); r->setStatusCode(k500InternalServerError); cb(r);
      }
    },
    {Post}
  );

  // POST /api/v1/auth/login  {username_or_email, password}
  app().registerHandler(
    "/api/v1/auth/login",
    [](const HttpRequestPtr& req, std::function<void (const HttpResponsePtr &)> &&cb) {
      Json::Value in; std::string perr;
      if (!parseJsonBody(req, &in, &perr)) {
        Json::Value err; err["error"] = "invalid_json"; err["detail"] = perr;
        auto r = HttpResponse::newHttpJsonResponse(err);
        r->setStatusCode(k400BadRequest); return cb(r);
      }

      std::string uoe = in.get("username_or_email","").asString();
      std::string password = in.get("password","").asString();
      if (uoe.empty() || password.empty()) {
        Json::Value err; err["error"] = "missing_fields";
        auto r = HttpResponse::newHttpJsonResponse(err);
        r->setStatusCode(k400BadRequest); return cb(r);
      }

      try {
        PGconn* conn = openConn();
        const char* params1[1] = { uoe.c_str() };
        PGresult* res = PQexecParams(conn,
          "SELECT id, username, password_hash FROM users WHERE username=$1 OR email=$1 LIMIT 1",
          1, nullptr, params1, nullptr, nullptr, 0);

        if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
          PQclear(res); PQfinish(conn);
          Json::Value err; err["error"] = "invalid_credentials";
          auto r = HttpResponse::newHttpJsonResponse(err);
          r->setStatusCode(k401Unauthorized); return cb(r);
        }

        std::string id    = PQgetvalue(res, 0, 0);
        std::string uname = PQgetvalue(res, 0, 1);
        std::string hash  = PQgetvalue(res, 0, 2);
        PQclear(res);

        if (!verifyPasswordArgon2id(password, hash)) {
          PQfinish(conn);
          Json::Value err; err["error"] = "invalid_credentials";
          auto r = HttpResponse::newHttpJsonResponse(err);
          r->setStatusCode(k401Unauthorized); return cb(r);
        }

        PQfinish(conn);
        const std::string secret = envOr("JWT_SECRET", "dev-secret-change-me");
        auto token = makeAccessTokenHS256(id, uname, secret, 60);

        Json::Value out; out["id"]=id; out["username"]=uname; out["accessToken"]=token;
        cb(HttpResponse::newHttpJsonResponse(out));
      } catch (const std::exception& e) {
        Json::Value j; j["error"]="login_exception"; j["detail"]=e.what();
        auto r = HttpResponse::newHttpJsonResponse(j); r->setStatusCode(k500InternalServerError); cb(r);
      }
    },
    {Post}
  );

  app().setDocumentRoot("public");  // serve static files from server/public


  app().run();
}
