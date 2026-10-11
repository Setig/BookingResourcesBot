#include "ServerApplication.h"

int main(int argc, char *argv[])
{
    ServerApplication app(argc, argv);
    const ServerApplication::ExitCode startCode = app.tryStart();
    if (startCode != ServerApplication::ExitCode::Success)
        return static_cast<int>(startCode);
    return app.exec();
}
