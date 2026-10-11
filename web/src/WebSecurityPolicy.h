#ifndef WEBSECURITYPOLICY_H
#define WEBSECURITYPOLICY_H

#include <QHostAddress>

namespace WebSecurityPolicy {

enum class PlainHttpPurpose
{
    Application,
    Redirect
};

[[nodiscard]] bool isPlainHttpBindAllowed(const QHostAddress &address,
                                          bool httpsEnabled,
                                          bool allowInsecureHttp);
[[nodiscard]] bool isPlainHttpBindAllowed(const QHostAddress &address,
                                          PlainHttpPurpose purpose,
                                          bool allowInsecureHttp);

} // namespace WebSecurityPolicy

#endif // WEBSECURITYPOLICY_H
