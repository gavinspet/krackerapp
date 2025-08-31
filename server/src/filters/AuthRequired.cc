#include "AuthRequired.h"
#include <drogon/drogon.h>
#include <regex>
#include "../utils/Jwt.h"

using namespace drogon;

static std::string envOr(const char* k, const char* d) {
  const char* v = std::getenv(k); return v? v : d;
}

void AuthRequired::doFilter(const HttpRequestPtr& req,
                            FilterCallback&& failed,
                            FilterChainCallback&& next) {
  // Expect "Authorization: Bearer <token>"
  auto auth = req->getHeader("authorization");
  std::smatch m;
  std::regex re("^Bearer\\s+(.+)$", std::regex::icase);
  if (!std::regex_search(auth, m, re)) {
    auto r = HttpResponse::newHttpJsonResponse(Json::Value{{"error","missing_bearer"}});
    r->setStatusCode(k401Unauthorized);
    return failed(r);
  }

  const auto token = m[1].str();
  const auto secret = envOr("JWT_SECRET", "dev-secret-change-me");

  std::string sub, username;
  if (!verifyAccessTokenHS256(token, secret, &sub, &username)) {
    auto r = HttpResponse::newHttpJsonResponse(Json::Value{{"error","invalid_token"}});
    r->setStatusCode(k401Unauthorized);
    return failed(r);
  }

  // Attach identity to the request for handlers to use
  req->attributes()->insert("user_id", sub);
  req->attributes()->insert("username", username);

  next(); // continue to the protected handler
}
