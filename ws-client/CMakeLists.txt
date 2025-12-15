add_executable(ws-client
    src/main.cpp
    src/ws_client.cpp
)

target_include_directories(ws-client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ws-client PRIVATE
    Boost::beast
    Boost::asio
    protocol-lib
    svckit
    fmt::fmt
    OpenSSL::SSL
    OpenSSL::Crypto
)

if(UNIX)
    target_link_libraries(ws-client PRIVATE pthread)
endif()
