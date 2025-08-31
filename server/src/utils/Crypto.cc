#include "Crypto.h"
#include <argon2.h>
#include <vector>
#include <random>
#include <stdexcept>

static std::string randomSalt(size_t len = 16) {
  std::random_device rd; std::mt19937 gen(rd());
  std::uniform_int_distribution<unsigned> dis(0, 255);
  std::string s; s.resize(len);
  for (size_t i=0;i<len;++i) s[i] = static_cast<char>(dis(gen));
  return s;
}

std::string hashPasswordArgon2id(const std::string& password) {
  // Dev-friendly defaults (increase for production)
  const uint32_t t_cost   = 3;        // iterations
  const uint32_t m_cost   = 1 << 16;  // memory (64 MiB)
  const uint32_t parallel = 1;        // lanes
  const size_t   hashlen  = 32;       // raw hash bytes (encoded output is longer)

  std::string salt = randomSalt(16);
  std::vector<char> encoded(256);     // ample space for encoded string

  int rc = argon2id_hash_encoded(
      t_cost, m_cost, parallel,
      password.data(), password.size(),
      reinterpret_cast<const uint8_t*>(salt.data()), salt.size(),
      hashlen,
      encoded.data(), encoded.size()
  );
  if (rc != ARGON2_OK) throw std::runtime_error("argon2id_hash_encoded failed");
  return std::string(encoded.data());
}

bool verifyPasswordArgon2id(const std::string& password, const std::string& encodedHash) {
  int rc = argon2id_verify(encodedHash.c_str(), password.data(), password.size());
  return rc == ARGON2_OK;
}
