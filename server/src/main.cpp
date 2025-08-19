#include <drogon/drogon.h>
using namespace drogon;

int main() {
  // Listen on 0.0.0.0:8080 (threads auto = number of cores)
  app().addListener("0.0.0.0", 8080);

  // GET /health -> {"ok": true}
  app().registerHandler("/health",
    [](const HttpRequestPtr&,
       std::function<void (const HttpResponsePtr &)> &&callback) {
        Json::Value body;
        body["ok"] = true;
        auto resp = HttpResponse::newHttpJsonResponse(body);
        callback(resp);
    },
    {Get});

  app().run();
}
