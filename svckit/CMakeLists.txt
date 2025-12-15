add_library(svckit INTERFACE)

target_include_directories(svckit INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(svckit INTERFACE
    OpenSSL::SSL
    OpenSSL::Crypto
)
