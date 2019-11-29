#pragma once

#include "NotificationX.h"

#include <memory>
#include <string>

struct IPublishSubscribeServer
{
    virtual void Push(const PlainNotification& notification) = 0;
    virtual ~IPublishSubscribeServer() {};
};

std::unique_ptr<IPublishSubscribeServer> MakePublishSubscribeServer(const std::string& serverIpAddress);
