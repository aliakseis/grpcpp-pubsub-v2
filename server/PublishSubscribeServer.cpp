#include <grpcpp/grpcpp.h>
#include <grpc/support/log.h>
#include <grpcpp/alarm.h>

#include "PublishSubscribe.grpc.pb.h"

#include "Delegate.h"

#include <boost/signals2/signal.hpp>

#include <memory>
#include <iostream>
#include <string>
#include <thread>
#include <deque>
#include <cassert>


using grpc::Server;
using grpc::ServerAsyncResponseWriter;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerCompletionQueue;
using grpc::Status;


class ServerImpl final {
public:
    ~ServerImpl() {
        server_->Shutdown();
        // Always shutdown the completion queue after the server.
        cq_->Shutdown();
    }

    // There is no shutdown handling in this code.
    void Run() {
        std::string server_address("0.0.0.0:50051");

        ServerBuilder builder;
        // Listen on the given address without any authentication mechanism.
        builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
        // Register "service_" as the instance through which we'll communicate with
        // clients. In this case it corresponds to an *asynchronous* service.
        builder.RegisterService(&observerService_);
        builder.RegisterService(&subscriberService_);
        // Get hold of the completion queue used for the asynchronous communication
        // with the gRPC runtime.
        cq_ = builder.AddCompletionQueue();
        // Finally assemble the server.
        server_ = builder.BuildAndStart();
        std::cout << "Server listening on " << server_address << std::endl;

        // Proceed to the server's main loop.
        HandleRpcs();
    }

private:
    class CallData {
    public:
        virtual void Proceed(bool ok) = 0;
    };
    // Class encompasing the state and logic needed to serve a request.
    class ObserverCallData : public CallData {
    public:
        // Take in the "service" instance (in this case representing an asynchronous
        // server) and the completion queue "cq" used for asynchronous communication
        // with the gRPC runtime.
        ObserverCallData(ServerImpl* parent)
            : parent_(parent)
            , responder_(&ctx_), status_(CREATE) {
            // Invoke the serving logic right away.
            Proceed(true);
        }

        void Proceed(bool ok) override {
            if (status_ == CREATE) {
                // Make this instance progress to the PROCESS state.
                status_ = PROCESS;

                // As part of the initial CREATE state, we *request* that the system
                // start processing SayHello requests. In this request, "this" acts are
                // the tag uniquely identifying the request (so that different CallData
                // instances can serve different requests concurrently), in this case
                // the memory address of this CallData instance.
                auto cq = parent_->cq_.get();
                parent_->observerService_.RequestNotify(&ctx_, &responder_, cq, cq, this);
            }
            else if (status_ == PROCESS) {
                // Spawn a new CallData instance to serve new clients while we process
                // the one for this CallData. The instance will deallocate itself as
                // part of its FINISH state.

                const bool wasStarted = started_;

                if (!started_)
                {
                    new ObserverCallData(parent_);
                    started_ = true;
                }

                // The actual processing.

                if (!ok)
                {
                    std::cout << "[ProceedM1]: Sending reply" << std::endl;
                    status_ = FINISH;
                    responder_.Finish(reply_, Status(), this);
                }
                else
                {
                    if (wasStarted)
                    {
                        if (request_.IsInitialized())
                        {
                            parent_->observer_(request_);
                        }
                    }

                    request_.Clear();
                    //PublishSubscribe::Notification().Swap(&request_);
                    responder_.Read(&request_, this);

                    // https://www.gresearch.co.uk/2019/03/20/lessons-learnt-from-writing-asynchronous-streaming-grpc-services-in-c/
                    status_ = PUSH_TO_BACK;
                }
            }
            else if (status_ == PUSH_TO_BACK)
            {
                status_ = PROCESS;
                alarm_.Set(parent_->cq_.get(), gpr_now(gpr_clock_type::GPR_CLOCK_REALTIME), this);
            }
            else {
                GPR_ASSERT(status_ == FINISH);
                // Once in the FINISH state, deallocate ourselves (CallData).
                delete this;
            }
        }

    private:
        // The means of communication with the gRPC runtime for an asynchronous server.
        // The producer-consumer queue where for asynchronous server notifications.

        ServerImpl* parent_;

        // Context for the rpc, allowing to tweak aspects of it such as the use
        // of compression, authentication, as well as to send metadata back to the
        // client.
        ServerContext ctx_;

        // What we get from the client.
        PublishSubscribe::Notification request_;

        // What we send back to the client.
        google::protobuf::Empty reply_;

        // The means to get back to the client.
        grpc::ServerAsyncReader<google::protobuf::Empty, PublishSubscribe::Notification> responder_;

        // Let's implement a tiny state machine with the following states.
        enum CallStatus { CREATE, PROCESS, FINISH, PUSH_TO_BACK };
        CallStatus status_;  // The current serving state.

        grpc::Alarm alarm_;

        bool started_ = false;
    };


    // Class encompasing the state and logic needed to serve a request.
    class SubscriberCallData : public CallData {
    public:
        // Take in the "service" instance (in this case representing an asynchronous
        // server) and the completion queue "cq" used for asynchronous communication
        // with the gRPC runtime.
        SubscriberCallData(ServerImpl* parent)
            : parent_(parent)
            , responder_(&ctx_), status_(CREATE) {
            // Invoke the serving logic right away.
            Proceed(true);
        }

        ~SubscriberCallData()
        {
            if (started_)
                parent_->observer_.disconnect(MakeDelegate<&SubscriberCallData::HandleNotification>(this));
        }

        void Proceed(bool ok) override {
            if (status_ == CREATE) {
                // Make this instance progress to the PROCESS state.
                status_ = PROCESS;

                // As part of the initial CREATE state, we *request* that the system
                // start processing SayHello requests. In this request, "this" acts are
                // the tag uniquely identifying the request (so that different CallData
                // instances can serve different requests concurrently), in this case
                // the memory address of this CallData instance.
                auto cq = parent_->cq_.get();
                parent_->subscriberService_.RequestSubscribe(&ctx_, &request_, &responder_, cq, cq,
                    this);
            }
            else if (status_ == PROCESS) {
                // Spawn a new CallData instance to serve new clients while we process
                // the one for this CallData. The instance will deallocate itself as
                // part of its FINISH state.
                if (!started_)
                {
                    new SubscriberCallData(parent_);

                    // subscribe to notifications
                    parent_->observer_.connect(MakeDelegate<&SubscriberCallData::HandleNotification>(this));

                    started_ = true;
                }

                // The actual processing.

                response_.Clear();

                // AsyncNotifyWhenDone?
                //if (ctx_.IsCancelled())
                //{
                //    std::cout << "[Proceed1M]: Trying finish" << std::endl;
                //    status_ = FINISH;
                //    responder_.Finish(Status(), this);
                //}
                //else 
                if (!fifo_.empty())
                {
                    response_ = fifo_.front();
                    fifo_.pop_front();

                    response_.set_content("n_" + response_.content());

                    responder_.Write(response_, this);

                    // https://www.gresearch.co.uk/2019/03/20/lessons-learnt-from-writing-asynchronous-streaming-grpc-services-in-c/
                    status_ = PUSH_TO_BACK;
                }
                else
                {
                    // https://www.gresearch.co.uk/2019/03/20/lessons-learnt-from-writing-asynchronous-streaming-grpc-services-in-c/
                    alarm_.Set(parent_->cq_.get(), gpr_now(gpr_clock_type::GPR_CLOCK_REALTIME), this);
                }
            }
            else if (status_ == PUSH_TO_BACK)
            {
                status_ = PROCESS;
                alarm_.Set(parent_->cq_.get(), gpr_now(gpr_clock_type::GPR_CLOCK_REALTIME), this);
            }
            else {
                GPR_ASSERT(status_ == FINISH);
                // Once in the FINISH state, deallocate ourselves (CallData).
                delete this;
            }
        }

        void HandleNotification(const PublishSubscribe::Notification& notification)
        {
            fifo_.push_back(notification);
        }

    private:
        // The means of communication with the gRPC runtime for an asynchronous server.
        // The producer-consumer queue where for asynchronous server notifications.
        ServerImpl* parent_;

        // Context for the rpc, allowing to tweak aspects of it such as the use
        // of compression, authentication, as well as to send metadata back to the
        // client.
        ServerContext ctx_;

        // What we get from the client.
        PublishSubscribe::NotificationChannel request_;

        // What we send back to the client.
        PublishSubscribe::Notification response_;

        // The means to get back to the client.
        grpc::ServerAsyncWriter<PublishSubscribe::Notification> responder_;

        // Let's implement a tiny state machine with the following states.
        enum CallStatus { CREATE, PROCESS, FINISH, PUSH_TO_BACK };
        CallStatus status_;  // The current serving state.

        std::deque<PublishSubscribe::Notification> fifo_;

        grpc::Alarm alarm_;

        bool started_ = false;
    };




    // This can be run in multiple threads if needed.
    void HandleRpcs() {
        // Spawn a new CallData instance to serve new clients.
        new ObserverCallData(this);
        new SubscriberCallData(this);
        void* tag;  // uniquely identifies a request.
        bool ok;
        while (true) {
            // Block waiting to read the next event from the completion queue. The
            // event is uniquely identified by its tag, which in this case is the
            // memory address of a CallData instance.
            // The return value of Next should always be checked. This return value
            // tells us whether there is any kind of event or cq_ is shutting down.
            GPR_ASSERT(cq_->Next(&tag, &ok));
            //GPR_ASSERT(ok);
            static_cast<CallData*>(tag)->Proceed(ok);
        }
    }

    std::unique_ptr<ServerCompletionQueue> cq_;
    PublishSubscribe::NotificationObserver::AsyncService observerService_;
    PublishSubscribe::NotificationSubscriber::AsyncService subscriberService_;
    std::unique_ptr<Server> server_;

    boost::signals2::signal<void(const PublishSubscribe::Notification&)> observer_;
};

int main(int argc, char** argv) {
    ServerImpl server;
    server.Run();

    return 0;
}
