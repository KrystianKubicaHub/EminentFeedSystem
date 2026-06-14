idf_component_register(
    SRCS
        "../Sdk/src/EminentSdk.cpp"
        "../Session_Manager/src/SessionManager.cpp"
        "../Transport_Layer/src/TransportLayer.cpp"
        "../Coding_Module/src/CodingModule.cpp"
        "../Physical_Layer/src/AbstractPhysicalLayer.cpp"
        "../Physical_Layer/src/PhysicalLayerEsp32Wifi.cpp"
        "../Crypto_Module/src/ChaCha20CryptoModule.cpp"
        "../Validation_Module/src/ValidationConfig.cpp"
        "../common/logging.cpp"

    INCLUDE_DIRS
        "../Sdk/include"
        "../Session_Manager/include"
        "../Transport_Layer/include"
        "../Coding_Module/include"
        "../Physical_Layer/include"
        "../Crypto_Module/include"
        "../Validation_Module/include"
        "../common"

    REQUIRES
        pthread
)

# C++17 required
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
