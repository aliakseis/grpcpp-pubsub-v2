#include "PublishSubscribeClient.h"

#include <grpcpp/grpcpp.h>
#include <grpc/support/log.h>

#include "PublishSubscribe.grpc.pb.h"

#include "Delegate.h"

#include <iostream>
#include <memory>
#include <string>
#include <cassert>

#include <signal.h>
#include <boost/signals2/signal.hpp>


using grpc::Channel;
using grpc::ClientAsyncResponseReader;
using grpc::ClientContext;
using grpc::CompletionQueue;
using grpc::Status;



namespace {


typedef boost::signals2::signal<void(void)> Terminator;

PlainNotification AsPlainNotification(const PublishSubscribe::Notification& src)
{
    PlainNotification result;
#define AS_PLAIN_NOTIFICATION_MACRO(type, name) result.name = src.name();
    NOTIFICATION_X(AS_PLAIN_NOTIFICATION_MACRO)
#undef AS_PLAIN_NOTIFICATION_MACRO
    return result;
}


//////////////////////////////////////////////////////////////////////////////

class GreeterClient : public IPublishSubscribeClient
{
public:
    explicit GreeterClient(
        std::shared_ptr<Channel> channel, const std::string& id,
        PublishSubscribeClientCallback callback)
        : stub_(PublishSubscribe::NotificationSubscriber::NewStub(channel))
        , callback_(callback)
    {
        GladToSeeMe(id);
        thread_ = std::thread(&GreeterClient::AsyncCompleteRpc, this);
    }
    ~GreeterClient()
    {
        thread_.join();
    }

    void TryCancel() override
    {
        terminator_();
    }

private:
    void GladToSeeMe(const std::string& id);
    void AsyncCompleteRpc();

private:
    friend class AsyncClientCall1M;
    // Out of the passed in Channel comes the stub, stored here, our view of the
    // server's exposed services.
    std::unique_ptr<PublishSubscribe::NotificationSubscriber::Stub> stub_;

    // The producer-consumer queue we use to communicate asynchronously with the
    // gRPC runtime.
    CompletionQueue cq_;

    PublishSubscribeClientCallback callback_;

    Terminator terminator_;

    std::thread thread_;

    std::atomic<int> numCalls_ = 0;
};


//////////////////////////////////////////////////////////////////////////////


// https://habr.com/ru/post/340758/
// https://github.com/Mityuha/grpc_async/blob/master/grpc_async_client.cc

class AsyncClientCall1M
{
    ClientContext context;
    PublishSubscribe::Notification reply;
    Status status{};
    enum CallStatus { START, PROCESS, FINISH, DESTROY } callStatus;
    std::unique_ptr< grpc::ClientAsyncReader<PublishSubscribe::Notification> > responder;

    GreeterClient* parent_;

public:
    AsyncClientCall1M(
        const PublishSubscribe::NotificationChannel& request, 
        GreeterClient* parent
    )
    : parent_(parent)
    {
        ++parent_->numCalls_;
        responder = parent_->stub_->AsyncSubscribe(&context, request, &parent_->cq_, this);
        parent_->terminator_.connect(MakeDelegate<&ClientContext::TryCancel>(&context));
        callStatus = START;
    }
    ~AsyncClientCall1M()
    {
        parent_->terminator_.disconnect(MakeDelegate<&ClientContext::TryCancel>(&context));
        --parent_->numCalls_;
    }

    void Proceed(bool ok = true)
    {
        switch (callStatus)
        {
        case PROCESS:
            // handle result
            // falls through
            if (ok)
            {
                parent_->callback_(AsPlainNotification(reply));
            }
        case START:
            if (!ok)
            {
                responder->Finish(&status, this);
                callStatus = FINISH;
                return;
            }
            callStatus = PROCESS;
            reply.Clear();
            responder->Read(&reply, this);
            break;
        case FINISH:
            delete this;
            break;
        }
    }
};


//////////////////////////////////////////////////////////////////////////////


void GreeterClient::GladToSeeMe(const std::string& id)
{
    PublishSubscribe::NotificationChannel request;
    request.set_id(id);
    new AsyncClientCall1M(request, this);// cq_, stub_, callback_, terminator_);
}


void GreeterClient::AsyncCompleteRpc()
{
    void* got_tag;
    bool ok = false;
    while (cq_.Next(&got_tag, &ok))
    {
        AsyncClientCall1M* call = static_cast<AsyncClientCall1M*>(got_tag);
        call->Proceed(ok);
        if (numCalls_ == 0)
            break;
    }
}


} // namespace

std::unique_ptr<IPublishSubscribeClient> MakePublishSubscribeClient(
    const std::string& targetIpAddress, const std::string& id, PublishSubscribeClientCallback callback)
{
    return std::make_unique<GreeterClient>(
        grpc::CreateChannel(targetIpAddress, grpc::InsecureChannelCredentials()), 
        id,
        callback);
}
