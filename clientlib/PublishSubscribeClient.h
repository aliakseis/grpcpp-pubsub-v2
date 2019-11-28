#pragma once

#include "NotificationX.h"

#include <functional>
#include <memory>
#include <string>

struct IPublishSubscribeClient
{
    virtual void TryCancel() = 0;
    virtual ~IPublishSubscribeClient() {};
};

typedef std::function<void(const PlainNotification&)> PublishSubscribeClientCallback;

std::unique_ptr<IPublishSubscribeClient> MakePublishSubscribeClient(
    const std::string& targetIpAddress, const std::string& id, PublishSubscribeClientCallback callback);
