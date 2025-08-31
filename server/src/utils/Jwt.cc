#include "Jwt.h"
#include <jwt-cpp/jwt.h>
#include <chrono>

std::string makeAccessTokenHS256(const std::string& sub,
                                 const std::string& username,
                                 const std::string& secret,
                                 int expires_minutes) {
  using namespace std::chrono;
  auto token = jwt::create()
    .set_subject(sub)
    .set_payload_claim("username", jwt::claim(username))
    .set_issued_at(system_clock::now())
    .set_expires_at(system_clock::now() + minutes(expires_minutes))
    .sign(jwt::algorithm::hs256{secret});
  return token;
}

bool verifyAccessTokenHS256(const std::string& token,
                            const std::string& secret,
                            std::string* outSub,
                            std::string* outUsername) {
  try {
    auto decoded = jwt::decode(token);
    jwt::verify()
      .allow_algorithm(jwt::algorithm::hs256{secret})
      .verify(decoded);

    if (outSub)      *outSub      = decoded.get_subject();
    if (outUsername) {
      if (decoded.has_payload_claim("username"))
        *outUsername = decoded.get_payload_claim("username").as_string();
      else *outUsername = "";
    }
    return true;
  } catch (...) { return false; }
}
