#include "WebApplication.h"

int main(int argc, char *argv[])
{
    WebApplication app(argc, argv);
    const WebApplication::ExitCode startCode = app.tryStart();
    if (startCode != WebApplication::ExitCode::Success)
        return static_cast<int>(startCode);
    return app.exec();
}
