include_directories(${CMAKE_SOURCE_DIR}/Physical_Layer/include)
include_directories(${CMAKE_SOURCE_DIR}/Crypto_Module/include)
add_library(CodingModule
    Coding_Module/src/CodingModule.cpp
)

target_include_directories(CodingModule PUBLIC
    Coding_Module/include
    Transport_Layer/include
    common
)

target_link_libraries(CodingModule PUBLIC common_utils)

add_library(common_utils
    common/logging.cpp
)

target_include_directories(common_utils PUBLIC
    common
)

add_library(validation_module
    Validation_Module/src/ValidationConfig.cpp
)

target_include_directories(validation_module PUBLIC
    Validation_Module/include
    common
)
include_directories(${CMAKE_SOURCE_DIR}/common)
include_directories(${CMAKE_SOURCE_DIR}/Sdk/include)
include_directories(${CMAKE_SOURCE_DIR}/Transport_Layer/include)
include_directories(${CMAKE_SOURCE_DIR}/Coding_Module/include)
include_directories(${CMAKE_SOURCE_DIR}/Validation_Module/include)

cmake_minimum_required(VERSION 3.10)
project(eminent_feed_system LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Coverage support
option(ENABLE_COVERAGE "Enable code coverage" OFF)
if(ENABLE_COVERAGE)
    message(STATUS "Code coverage enabled")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --coverage -fprofile-arcs -ftest-coverage")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
endif()

add_library(eminent_sdk
    Sdk/src/EminentSdk.cpp
)

target_include_directories(eminent_sdk PUBLIC
    Sdk/include
    Session_Manager/include
    Transport_Layer/include
    Physical_Layer/include
    Coding_Module/include
    Validation_Module/include
    Crypto_Module/include
    common
)

target_link_libraries(eminent_sdk PUBLIC common_utils)
target_link_libraries(eminent_sdk PUBLIC validation_module)
target_link_libraries(eminent_sdk PUBLIC crypto_module)

add_library(session_manager
    Session_Manager/src/SessionManager.cpp
)

target_include_directories(session_manager PUBLIC
    Session_Manager/include
)

target_link_libraries(session_manager PUBLIC common_utils)


add_library(transport_layer
    Transport_Layer/src/TransportLayer.cpp
)

target_include_directories(transport_layer PUBLIC
    Transport_Layer/include
    Session_Manager/include
)

target_link_libraries(transport_layer PUBLIC common_utils)



add_library(physical_layer
    Physical_Layer/src/AbstractPhysicalLayer.cpp
    Physical_Layer/src/PhysicalLayerUdp.cpp
    Physical_Layer/src/PhysicalLayerInMemory.cpp
)

target_include_directories(physical_layer PUBLIC
    Physical_Layer/include
)

target_link_libraries(physical_layer PUBLIC common_utils)

add_library(crypto_module
    Crypto_Module/src/ChaCha20CryptoModule.cpp
)

target_include_directories(crypto_module PUBLIC
    Crypto_Module/include
)

target_link_libraries(crypto_module PUBLIC common_utils)


# Tworzymy tylko główny plik wykonywalny z app/main.cpp
add_executable(eminent_demo
    app/main.cpp
)

target_link_libraries(eminent_demo
    eminent_sdk
    session_manager
    transport_layer
    physical_layer
    CodingModule
    common_utils
    validation_module
)

target_include_directories(eminent_demo PRIVATE
    Sdk/include
    Session_Manager/include
    Transport_Layer/include
    Physical_Layer/include
    Validation_Module/include
)

add_executable(test_stabilnosci_2_synchroniczny
    app/test_stabilnosci_2_synchroniczny.cpp
)

target_link_libraries(test_stabilnosci_2_synchroniczny
    eminent_sdk
    session_manager
    transport_layer
    physical_layer
    CodingModule
    common_utils
    validation_module
)

target_include_directories(test_stabilnosci_2_synchroniczny PRIVATE
    Sdk/include
    Session_Manager/include
    Transport_Layer/include
    Physical_Layer/include
    Validation_Module/include
)

# ============================================================
# Unit Tests (Google Test)
# ============================================================

include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

set(TEST_LIBS
    eminent_sdk
    session_manager
    transport_layer
    physical_layer
    CodingModule
    common_utils
    validation_module
    crypto_module
    gtest
    gtest_main
)

set(TEST_INCLUDES
    Sdk/include
    Session_Manager/include
    Transport_Layer/include
    Physical_Layer/include
    Coding_Module/include
    Validation_Module/include
    Crypto_Module/include
    common
)

# SDK tests
add_executable(test_sdk Sdk/tests/test_sdk.cpp)
target_link_libraries(test_sdk ${TEST_LIBS})
target_include_directories(test_sdk PRIVATE ${TEST_INCLUDES})
add_test(NAME test_sdk COMMAND test_sdk)

# Transport Layer tests
add_executable(test_transport_layer Transport_Layer/tests/test_transport_layer.cpp)
target_link_libraries(test_transport_layer ${TEST_LIBS})
target_include_directories(test_transport_layer PRIVATE ${TEST_INCLUDES})
add_test(NAME test_transport_layer COMMAND test_transport_layer)

# Session Manager tests
add_executable(test_session_manager Session_Manager/tests/test_session_manager.cpp)
target_link_libraries(test_session_manager ${TEST_LIBS})
target_include_directories(test_session_manager PRIVATE ${TEST_INCLUDES})
add_test(NAME test_session_manager COMMAND test_session_manager)

# Physical Layer tests
add_executable(test_physical_layer Physical_Layer/tests/test_physical_layer.cpp)
target_link_libraries(test_physical_layer ${TEST_LIBS})
target_include_directories(test_physical_layer PRIVATE ${TEST_INCLUDES})
add_test(NAME test_physical_layer COMMAND test_physical_layer)

# Crypto Module tests
add_executable(test_crypto Crypto_Module/tests/test_crypto.cpp)
target_link_libraries(test_crypto ${TEST_LIBS})
target_include_directories(test_crypto PRIVATE ${TEST_INCLUDES})
add_test(NAME test_crypto COMMAND test_crypto)
