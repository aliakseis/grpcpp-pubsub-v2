#include "PublishSubscribeServer.h"

#include <thread>

int main()
{
    auto server = MakePublishSubscribeServer();

    std::thread thread_ = std::thread([&server] { server->Run("0.0.0.0:50051"); });
    //server->Run("0.0.0.0:50051");

    for (unsigned int i = 0; i < 1000000; ++i)
    {
        server->Push({ i, "n_" + std::to_string(i) });
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    thread_.join();
}
