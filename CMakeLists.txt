include_directories(${CMAKE_SOURCE_DIR}/physical_layer/include)
add_library(CodingModule
    CodingModule/src/CodingModule.cpp
)

target_include_directories(CodingModule PUBLIC
    CodingModule/include
    transport_layer/include
    common
)
include_directories(${CMAKE_SOURCE_DIR}/common)
include_directories(${CMAKE_SOURCE_DIR}/sdk/include)
include_directories(${CMAKE_SOURCE_DIR}/transport_layer/include)
include_directories(${CMAKE_SOURCE_DIR}/CodingModule/include)

cmake_minimum_required(VERSION 3.10)
project(eminent_feed_system LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(eminent_sdk
    sdk/src/eminent_sdk.cpp
)

target_include_directories(eminent_sdk PUBLIC
    sdk/include
    session_manager/include
    transport_layer/include
    physical_layer/include
    CodingModule/include
    common
)

add_library(session_manager
    session_manager/src/session_manager.cpp
)

target_include_directories(session_manager PUBLIC
    session_manager/include
)


add_library(transport_layer
    transport_layer/src/transport_layer.cpp
)

target_include_directories(transport_layer PUBLIC
    transport_layer/include
    session_manager/include
)



add_library(physical_layer
    physical_layer/src/physical_layer.cpp
)

target_include_directories(physical_layer PUBLIC
    physical_layer/include
)


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
)

target_include_directories(eminent_demo PRIVATE
    sdk/include
    session_manager/include
    transport_layer/include
    physical_layer/include
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
)

target_include_directories(test_stabilnosci_2_synchroniczny PRIVATE
    sdk/include
    session_manager/include
    transport_layer/include
    physical_layer/include
)
