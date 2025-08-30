#include <drogon/drogon.h>
#include <json/json.h>
#include <filesystem>
#include <libpq-fe.h>

using namespace drogon;

static bool loadConfigSmart() {
  namespace fs = std::filesystem;
  if (fs::exists("config.json")) { app().loadConfigFile("config.json"); return true; }
  if (fs::exists("server/config.json")) { app().loadConfigFile("server/config.json"); return true; }
  return false;
}

int main() {
  // Log CWD
  char cwd[4096]; if (getcwd(cwd, sizeof(cwd))) { LOG_INFO << "CWD: " << cwd; }
  if (!loadConfigSmart()) { LOG_WARN << "No config.json found (ok for this step)"; }
  app().setLogLevel(trantor::Logger::kInfo);

  // /health
  app().registerHandler(
    "/health",
    [](const HttpRequestPtr&, std::function<void (const HttpResponsePtr &)> &&cb) {
      Json::Value j; j["ok"]=true;
      cb(HttpResponse::newHttpJsonResponse(j));
    },
    {Get}
  );

  // /db/health using libpq directly (no Drogon ORM needed)
  app().registerHandler(
    "/db/health",
    [](const HttpRequestPtr&, std::function<void (const HttpResponsePtr &)> &&cb) {
      Json::Value j;
      const char* conninfo = "host=127.0.0.1 port=5432 dbname=krackerdb user=kracker password=krackerpw";
      PGconn* conn = PQconnectdb(conninfo);
      if (PQstatus(conn) == CONNECTION_OK) {
        PGresult* res = PQexec(conn, "SELECT 1");
        if (PQresultStatus(res) == PGRES_TUPLES_OK) {
          j["ok"]=true; j["db"]="up";
        } else {
          j["ok"]=false; j["db"]="down"; j["error"]="SELECT 1 failed";
        }
        PQclear(res);
      } else {
        j["ok"]=false; j["db"]="down"; j["error"]=PQerrorMessage(conn);
      }
      PQfinish(conn);
      cb(HttpResponse::newHttpJsonResponse(j));
    },
    {Get}
  );

  app().run();
}
