# Applications may keep JetConfig.hpp in a frontend-specific directory.
# Allow an explicit override while retaining the original main/ layout.
if(NOT DEFINED JET_CONFIG_INCLUDE_DIR)
    if(EXISTS "${PROJECT_DIR}/main/firmware/JetConfig.hpp")
        set(JET_CONFIG_INCLUDE_DIR "${PROJECT_DIR}/main/firmware")
    else()
        set(JET_CONFIG_INCLUDE_DIR "${PROJECT_DIR}/main")
    endif()
endif()

idf_component_register(SRC_DIRS "src"
                       INCLUDE_DIRS "src"
                       PRIV_INCLUDE_DIRS "${JET_CONFIG_INCLUDE_DIR}"
                       )

# Jet requires C++17
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
