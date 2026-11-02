# The online backend: The Hunter's Dream (thd/), whose own sceNet, NetCtl and NP functions answer
# the game's network imports, and shadPS4 v.0.20.0's HTTP and SSL libraries
# (529ceb32f5546b820588f194b903cc39d08015fe), which its sceHttp and sceSsl imports call.
# The renderer and the existing shared runtime shims stay authoritative.
find_package(nlohmann_json CONFIG REQUIRED GLOBAL)
find_package(OpenSSL REQUIRED GLOBAL)
find_package(ZLIB REQUIRED GLOBAL)
file(GLOB BBNET_THD_SOURCES CONFIGURE_DEPENDS thd/*.cpp)
add_library(bbnet STATIC ${BBNET_THD_SOURCES} bbnet_glue.cpp
    vendor/shadps4/core/libraries/network/http.cpp vendor/shadps4/core/libraries/network/ssl2.cpp)
target_include_directories(bbnet PRIVATE include ../shim vendor/shadps4 ../shadps4 third_party/httplib)
target_compile_definitions(bbnet PRIVATE NDEBUG CPPHTTPLIB_OPENSSL_SUPPORT CPPHTTPLIB_ZLIB_SUPPORT)
target_compile_options(bbnet PRIVATE -Wno-deprecated-declarations -Wno-missing-field-initializers)
target_link_libraries(bbnet PUBLIC fmt::fmt nlohmann_json::nlohmann_json OpenSSL::SSL OpenSSL::Crypto ZLIB::ZLIB)
if(WIN32)
    target_compile_definitions(bbnet PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(bbnet PUBLIC ws2_32 iphlpapi bcrypt crypt32)
endif()
