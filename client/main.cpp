#include "PublishSubscribeClient.h"

#include <iostream>

int main()
{
    auto lam = [](const PlainNotification& notification) {
        std::cout << notification.index << ' ' << notification.content << ',';
    };
    auto client = MakePublishSubscribeClient("localhost:50051", "42", lam);
}
