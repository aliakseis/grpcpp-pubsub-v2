
#include <grpcpp/grpcpp.h>
#include <grpc/support/log.h>

#include "PublishSubscribe.grpc.pb.h"

#include <iostream>
#include <memory>
#include <string>
#include <cassert>


using grpc::Channel;
using grpc::ClientAsyncResponseReader;
using grpc::ClientContext;
using grpc::CompletionQueue;
using grpc::Status;

// https://habr.com/ru/post/340758/
// https://github.com/Mityuha/grpc_async/blob/master/grpc_async_client.cc

class AsyncClientCallM1
{
    ClientContext context;
    ::google::protobuf::Empty reply;
    Status status{};
    enum CallStatus { PROCESS, FINISH, DESTROY } callStatus;
    std::unique_ptr< grpc::ClientAsyncWriter<PublishSubscribe::Notification> > responder;
    unsigned mcounter;
    bool writing_mode_;

public:
    AsyncClientCallM1(CompletionQueue& cq_, std::unique_ptr<PublishSubscribe::NotificationObserver::Stub>& stub_) :
        mcounter(0), writing_mode_(true)
    {
        std::cout << "[ProceedM1]: new client M-1" << std::endl;
        responder = stub_->AsyncNotify(&context, &reply, &cq_, (void*)this);
        callStatus = PROCESS;
    }
    void Proceed(bool ok = true)
    {
        if (callStatus == PROCESS)
        {
            if (writing_mode_)
            {
                //std::cout << "[ProceedM1]: mcounter = " << mcounter << std::endl;
                //if (mcounter < greeting.size())
                {
                    PublishSubscribe::Notification request;
                    request.set_content(std::to_string(mcounter));
                    responder->Write(request, (void*)this);
                    ++mcounter;
                }
                //else
                //{
                //    responder->WritesDone((void*)this);
                //    std::cout << "[ProceedM1]: changing state to reading" << std::endl;
                //    writing_mode_ = false;
                //    return;
                //}
            }
            else//reading mode
            {
                std::cout << "[ProceedM1]: trying finish" << std::endl;
                responder->Finish(&status, (void*)this);
                callStatus = FINISH;
            }
        }
        else if (callStatus == FINISH)
        {
            //assert(!reply.message().empty());
            //printReply("ProceedM1");
            //std::cout << "[ProceedM1]: Good Bye" << std::endl;
            delete this;
        }
        return;
    }
};



/*

class GreeterClient {
public:
    explicit GreeterClient(std::shared_ptr<Channel> channel)
        : stub_(PublishSubscribe::NotificationObserver::NewStub(channel)) {}

    // Assembles the client's payload, sends it and presents the response back
    // from the server.
    std::string SayHello(const std::string& user) {
        // Data we are sending to the server.
        //PublishSubscribe:: request;
        request.set_name(user);

        // Container for the data we expect from the server.
        HelloReply reply;

        // Context for the client. It could be used to convey extra information to
        // the server and/or tweak certain RPC behaviors.
        ClientContext context;

        // The producer-consumer queue we use to communicate asynchronously with the
        // gRPC runtime.
        CompletionQueue cq;

        // Storage for the status of the RPC upon completion.
        Status status;

        // stub_->PrepareAsyncSayHello() creates an RPC object, returning
        // an instance to store in "call" but does not actually start the RPC
        // Because we are using the asynchronous API, we need to hold on to
        // the "call" instance in order to get updates on the ongoing RPC.
        std::unique_ptr<ClientAsyncResponseReader<HelloReply> > rpc(
            stub_->PrepareAsyncSayHello(&context, request, &cq));

        // StartCall initiates the RPC call
        rpc->StartCall();

        // Request that, upon completion of the RPC, "reply" be updated with the
        // server's response; "status" with the indication of whether the operation
        // was successful. Tag the request with the integer 1.
        rpc->Finish(&reply, &status, (void*)1);
        void* got_tag;
        bool ok = false;
        // Block until the next result is available in the completion queue "cq".
        // The return value of Next should always be checked. This return value
        // tells us whether there is any kind of event or the cq_ is shutting down.
        GPR_ASSERT(cq.Next(&got_tag, &ok));

        // Verify that the result from "cq" corresponds, by its tag, our previous
        // request.
        GPR_ASSERT(got_tag == (void*)1);
        // ... and that the request was completed successfully. Note that "ok"
        // corresponds solely to the request for updates introduced by Finish().
        GPR_ASSERT(ok);

        // Act upon the status of the actual RPC.
        if (status.ok()) {
            return reply.message();
        }
        else {
            return "RPC failed";
        }
    }

private:
    // Out of the passed in Channel comes the stub, stored here, our view of the
    // server's exposed services.
    std::unique_ptr<PublishSubscribe::NotificationObserver::Stub> stub_;
};

*/



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
