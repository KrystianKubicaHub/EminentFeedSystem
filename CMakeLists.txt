cmake_minimum_required(VERSION 3.10)
project(eminent_feed_system LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(eminent_sdk
    sdk/src/eminent_sdk.cpp
)

target_include_directories(eminent_sdk PUBLIC
    sdk/include
)

add_library(session_manager
    session_manager/src/session_manager.cpp
)

target_include_directories(session_manager PUBLIC
    session_manager/include
)

enable_testing()
find_package(GTest REQUIRED)

add_executable(test_sdk
    sdk/tests/test_sdk.cpp
)

target_link_libraries(test_sdk
    eminent_sdk
    GTest::GTest
    GTest::Main
)

target_include_directories(test_sdk PRIVATE
    sdk/include
)

add_test(NAME test_sdk COMMAND test_sdk)

add_executable(test_session_manager
    session_manager/tests/test_session_manager.cpp
)

target_link_libraries(test_session_manager
    session_manager
    GTest::GTest
    GTest::Main
)

target_include_directories(test_session_manager PRIVATE
    session_manager/include
)

add_test(NAME test_session_manager COMMAND test_session_manager)

add_library(transport_layer
    transport_layer/src/transport_layer.cpp
)

target_include_directories(transport_layer PUBLIC
    transport_layer/include
)

add_executable(test_transport_layer
    transport_layer/tests/test_transport_layer.cpp
)

target_link_libraries(test_transport_layer
    transport_layer
    GTest::GTest
    GTest::Main
)

target_include_directories(test_transport_layer PRIVATE
    transport_layer/include
)

add_test(NAME test_transport_layer COMMAND test_transport_layer)


add_library(physical_layer
    physical_layer/src/physical_layer.cpp
)

target_include_directories(physical_layer PUBLIC
    physical_layer/include
)

add_executable(test_physical_layer
    physical_layer/tests/test_physical_layer.cpp
)

target_link_libraries(test_physical_layer
    physical_layer
    GTest::GTest
    GTest::Main
)

target_include_directories(test_physical_layer PRIVATE
    physical_layer/include
)

add_test(NAME test_physical_layer COMMAND test_physical_layer)
