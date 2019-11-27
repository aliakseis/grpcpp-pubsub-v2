
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


boost::signals2::signal<void(void)> terminator;

void signalHandler(int signo)
{
    terminator();
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


// https://habr.com/ru/post/340758/
// https://github.com/Mityuha/grpc_async/blob/master/grpc_async_client.cc

class AsyncClientCallM1
{
    ClientContext context;
    PublishSubscribe::Notification request;
    ::google::protobuf::Empty reply;
    Status status{};
    enum CallStatus { PROCESS, FINISH, DESTROY } callStatus;
    std::unique_ptr< grpc::ClientAsyncWriter<PublishSubscribe::Notification> > responder;
    unsigned mcounter;
    bool writing_mode_;

public:
    AsyncClientCallM1(CompletionQueue& cq_, std::unique_ptr<PublishSubscribe::NotificationObserver::Stub>& stub_) 
        : mcounter(0), writing_mode_(true)
    {
        std::cout << "[ProceedM1]: new client M-1" << std::endl;
        responder = stub_->AsyncNotify(&context, &reply, &cq_, this);
        terminator.connect(MakeDelegate<&ClientContext::TryCancel>(&context));
        callStatus = PROCESS;
    }
    ~AsyncClientCallM1()
    {
        terminator.disconnect(MakeDelegate<&ClientContext::TryCancel>(&context));
    }
    void Proceed(bool ok = true)
    {
        if (callStatus == PROCESS)
        {
            if (!ok)
            {
                responder->Finish(&status, this);
                callStatus = FINISH;
                return;
            }
            if (writing_mode_)
            {
                request.set_content(std::to_string(mcounter));
                responder->Write(request, this);
                ++mcounter;
                std::cout << request.content() << ',';
            }
            else//reading mode
            {
                std::cout << "[ProceedM1]: trying finish" << std::endl;
                responder->Finish(&status, this);
                callStatus = FINISH;
            }
        }
        else if (callStatus == FINISH)
        {
            delete this;
        }
        return;
    }
};



class GreeterClient
{
public:
    explicit GreeterClient(std::shared_ptr<Channel> channel)
        :stub_(PublishSubscribe::NotificationObserver::NewStub(channel))
    {
        GladToSeeYou();
    }


    void GladToSeeYou()
    {
        new AsyncClientCallM1(cq_, stub_);
    }


    void AsyncCompleteRpc()
    {
        void* got_tag;
        bool ok = false;
        while (cq_.Next(&got_tag, &ok))
        {
            AsyncClientCallM1* call = static_cast<AsyncClientCallM1*>(got_tag);
            call->Proceed(ok);
        }
        std::cout << "Completion queue is shutting down." << std::endl;
    }

private:
    // Out of the passed in Channel comes the stub, stored here, our view of the
    // server's exposed services.
    std::unique_ptr<PublishSubscribe::NotificationObserver::Stub> stub_;

    // The producer-consumer queue we use to communicate asynchronously with the
    // gRPC runtime.
    CompletionQueue cq_;
};



int main(/*int argc, char** argv*/) {
    // Instantiate the client. It requires a channel, out of which the actual RPCs
    // are created. This channel models a connection to an endpoint (in this case,
    // localhost at port 50051). We indicate that the channel isn't authenticated
    // (use of InsecureChannelCredentials()).

    GreeterClient greeter(grpc::CreateChannel("localhost:50051", grpc::InsecureChannelCredentials()));
    std::thread thread_ = std::thread(&GreeterClient::AsyncCompleteRpc, &greeter);
    thread_.join();

    return 0;
}
