#pragma once

#include "DeveloperApi.h"
#include <memory>

namespace ce::developer
{
class HttpTransport {
public:
    HttpTransport(DeveloperApi& api, const Settings& settings);
    ~HttpTransport();
    int Start(int port);
    void Stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace ce::developer
