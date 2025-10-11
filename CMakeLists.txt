include_directories(${CMAKE_SOURCE_DIR}/Physical_Layer/include)
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
    common
)

target_link_libraries(eminent_sdk PUBLIC common_utils)
target_link_libraries(eminent_sdk PUBLIC validation_module)

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
    Physical_Layer/src/PhysicalLayer.cpp
)

target_include_directories(physical_layer PUBLIC
    Physical_Layer/include
)

target_link_libraries(physical_layer PUBLIC common_utils)


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
