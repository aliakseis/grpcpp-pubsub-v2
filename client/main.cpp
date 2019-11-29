#include "PublishSubscribeClient.h"

#include <signal.h>

#include <iostream>

static std::unique_ptr<IPublishSubscribeClient> client;

void signalHandler(int signo)
{
    if (client) {
        client->TryCancel();
    }
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
    auto lam = [](const PlainNotification& notification) {
        std::cout << notification.index << ' ' << notification.content << ',';
    };
    client = MakePublishSubscribeClient("localhost:50051", "42", lam);
}
