idf_component_register(SRC_DIRS "src"
                       INCLUDE_DIRS "src"
                       PRIV_INCLUDE_DIRS "${PROJECT_DIR}/main"
                       )

# Jet requires C++17
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
