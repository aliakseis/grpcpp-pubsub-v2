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


//boost::signals2::signal<void(void)> terminator;
//
//void signalHandler(int signo)
//{
//    terminator();
//}
//
//
//void setSignalHandler()
//{
//#ifdef _WIN32
//    signal(SIGINT, signalHandler);
//#else
//    struct sigaction sa;
//    sa.sa_handler = signalHandler;
//    sigemptyset(&sa.sa_mask);
//    sa.sa_flags = 0;
//    sigaction(SIGINT, &sa, NULL);
//#endif
//}


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


// https://habr.com/ru/post/340758/
// https://github.com/Mityuha/grpc_async/blob/master/grpc_async_client.cc

class AsyncClientCall1M
{
    ClientContext context;
    PublishSubscribe::Notification reply;
    Status status{};
    enum CallStatus { START, PROCESS, FINISH, DESTROY } callStatus;
    std::unique_ptr< grpc::ClientAsyncReader<PublishSubscribe::Notification> > responder;

    PublishSubscribeClientCallback callback_;
    std::weak_ptr<Terminator> terminator_;

public:
    AsyncClientCall1M(const PublishSubscribe::NotificationChannel& request, 
        CompletionQueue& cq_, 
        std::unique_ptr<PublishSubscribe::NotificationSubscriber::Stub>& stub_,
        PublishSubscribeClientCallback callback,
        std::shared_ptr<Terminator> terminator)
    : callback_(callback)
    , terminator_(terminator)
    {
        //std::cout << "[Proceed1M]: new client 1-M" << std::endl;
        responder = stub_->AsyncSubscribe(&context, request, &cq_, this);
        terminator->connect(MakeDelegate<&ClientContext::TryCancel>(&context));
        callStatus = START;
    }
    ~AsyncClientCall1M()
    {
        if (auto obj = terminator_.lock())
        {
            obj->disconnect(MakeDelegate<&ClientContext::TryCancel>(&context));
        }
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
                callback_(AsPlainNotification(reply));
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
            //std::cout << "[Proceed1M]: Good Bye" << std::endl;
            delete this;
            break;
        }
    }
};


class GreeterClient : public IPublishSubscribeClient
{
public:
    explicit GreeterClient(
        std::shared_ptr<Channel> channel, const std::string& id,
        PublishSubscribeClientCallback callback)
    : stub_(PublishSubscribe::NotificationSubscriber::NewStub(channel))
    , callback_(callback)
    , terminator_(std::make_shared<Terminator>())
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
        (*terminator_)();
    }

private:
    void GladToSeeMe(const std::string& id)
    {
        PublishSubscribe::NotificationChannel request;
        request.set_id(id);
        new AsyncClientCall1M(request, cq_, stub_, callback_, terminator_);
    }


    void AsyncCompleteRpc()
    {
        void* got_tag;
        bool ok = false;
        while (cq_.Next(&got_tag, &ok))
        {
            AsyncClientCall1M* call = static_cast<AsyncClientCall1M*>(got_tag);
            call->Proceed(ok);
        }
        std::cout << "Completion queue is shutting down." << std::endl;
    }

private:
    // Out of the passed in Channel comes the stub, stored here, our view of the
    // server's exposed services.
    std::unique_ptr<PublishSubscribe::NotificationSubscriber::Stub> stub_;

    // The producer-consumer queue we use to communicate asynchronously with the
    // gRPC runtime.
    CompletionQueue cq_;

    PublishSubscribeClientCallback callback_;

    std::shared_ptr<Terminator> terminator_;

    std::thread thread_;
};

} // namespace

std::unique_ptr<IPublishSubscribeClient> MakePublishSubscribeClient(
    const std::string& targetIpAddress, const std::string& id, PublishSubscribeClientCallback callback)
{
    return std::make_unique<GreeterClient>(
        grpc::CreateChannel(targetIpAddress, grpc::InsecureChannelCredentials()), 
        id,
        callback);
}
