add_executable(ws-server
    src/main.cpp
    src/ws_server.cpp
)

target_include_directories(ws-server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ws-server PRIVATE
    Boost::beast
    Boost::asio
    protocol-lib
    svckit
    fmt::fmt
    OpenSSL::SSL
    OpenSSL::Crypto
)

if(UNIX)
    target_link_libraries(ws-server PRIVATE pthread)
endif()
