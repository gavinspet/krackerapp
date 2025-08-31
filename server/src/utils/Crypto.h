#pragma once
#include <string>

std::string hashPasswordArgon2id(const std::string& password);
bool verifyPasswordArgon2id(const std::string& password, const std::string& encodedHash);
