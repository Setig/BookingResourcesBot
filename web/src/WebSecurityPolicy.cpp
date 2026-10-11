#include "WebSecurityPolicy.h"

bool WebSecurityPolicy::isPlainHttpBindAllowed(const QHostAddress &address,
                                               bool httpsEnabled,
                                               bool allowInsecureHttp)
{
    return httpsEnabled || allowInsecureHttp || address.isLoopback();
}

bool WebSecurityPolicy::isPlainHttpBindAllowed(const QHostAddress &address,
                                               PlainHttpPurpose purpose,
                                               bool allowInsecureHttp)
{
    return purpose == PlainHttpPurpose::Redirect || allowInsecureHttp || address.isLoopback();
}
