#pragma once
#include <drogon/HttpFilter.h>

class AuthRequired : public drogon::HttpFilter<AuthRequired> {
public:
  void doFilter(const drogon::HttpRequestPtr&,
                drogon::FilterCallback&&,
                drogon::FilterChainCallback&&) override;
};
