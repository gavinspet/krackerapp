 #pragma once
#include <string>

std::string makeAccessTokenHS256(const std::string& sub,
                                 const std::string& username,
                                 const std::string& secret,
                                 int expires_minutes = 15);

bool verifyAccessTokenHS256(const std::string& token,
                            const std::string& secret,
                            std::string* outSub,
                            std::string* outUsername);
