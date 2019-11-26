cmake_minimum_required(VERSION 3.4)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


project(PublishSubscribe CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type (Debug, Release, RelWithDebInfo, MinSizeRel)" FORCE)
endif()

#add_compile_options(-Wall -Werror)

if(MSVC)
  add_definitions(-D_WIN32_WINNT=0x600)
endif()


find_package(Protobuf REQUIRED)


find_package(gRPC CONFIG REQUIRED)
message(STATUS "Using gRPC ${gRPC_VERSION}")
set(GRPC_CPP_LIB gRPC::grpc++_unsecure)
set(GRPC_PLUGIN_EXEC $<TARGET_FILE:gRPC::grpc_cpp_plugin>)


get_filename_component(PROTO_FILE "./common/PublishSubscribe.proto" ABSOLUTE)
get_filename_component(PROTO_PATH "${PROTO_FILE}" PATH)
get_filename_component(PROTO_NAME "${PROTO_FILE}" NAME_WE)

#generated sources
set(PROTO_SRCS "${CMAKE_CURRENT_BINARY_DIR}/${PROTO_NAME}.pb.cc")
set(PROTO_HDRS "${CMAKE_CURRENT_BINARY_DIR}/${PROTO_NAME}.pb.h")
set(GRPC_SRCS "${CMAKE_CURRENT_BINARY_DIR}/${PROTO_NAME}.grpc.pb.cc")
set(GRPC_HDRS "${CMAKE_CURRENT_BINARY_DIR}/${PROTO_NAME}.grpc.pb.h")

add_custom_command(
    OUTPUT "${PROTO_SRCS}" "${PROTO_HDRS}" "${GRPC_SRCS}" "${GRPC_HDRS}"
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
    ARGS --grpc_out "${CMAKE_CURRENT_BINARY_DIR}"
        --cpp_out "${CMAKE_CURRENT_BINARY_DIR}"
        -I "${PROTO_PATH}"
        --plugin=protoc-gen-grpc="${GRPC_PLUGIN_EXEC}"
        "${PROTO_FILE}"
    DEPENDS "${PROTO_FILE}")


include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_executable(PublishSubscribeServer
               server/PublishSubscribeServer.cpp
               ${PROTO_SRCS}
               ${GRPC_SRCS})
target_include_directories(PublishSubscribeServer PRIVATE ./server ./common)
target_link_libraries(PublishSubscribeServer
                      ${GRPC_CPP_LIB}
                      ${Protobuf_LIBRARIES})

add_executable(PublishSubscribeClient
               client/PublishSubscribeClient.cpp
               ${PROTO_SRCS}
               ${GRPC_SRCS})
target_include_directories(PublishSubscribeClient PRIVATE ./client ./common)
target_link_libraries(PublishSubscribeClient
                      ${GRPC_CPP_LIB}
                      ${Protobuf_LIBRARIES})

add_executable(PublishSubscribeNotifier
               notifier/PublishSubscribeNotifier.cpp
               ${PROTO_SRCS}
               ${GRPC_SRCS})
target_include_directories(PublishSubscribeNotifier PRIVATE ./notifier ./common)
target_link_libraries(PublishSubscribeNotifier
                      ${GRPC_CPP_LIB}
                      ${Protobuf_LIBRARIES})
