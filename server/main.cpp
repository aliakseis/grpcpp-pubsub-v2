#include "PublishSubscribeServer.h"

#include <atomic>
#include <signal.h>
#include <thread>


std::atomic_bool shutdownRequested(false);


void signalHandler(int signo)
{
    shutdownRequested = true;
}


void setSignalHandler()
{
#ifdef _WIN32
    signal(SIGINT, signalHandler);
#else
    struct sigaction sa;
    sa.sa_handler = signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
#endif
}


int main()
{
    setSignalHandler();

    auto server = MakePublishSubscribeServer("0.0.0.0:50051");

    for (unsigned int i = 0; i < 1000000 && !shutdownRequested; ++i)
    {
        server->Push({ i, "n_" + std::to_string(i) });
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
