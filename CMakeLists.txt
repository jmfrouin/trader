cmake_minimum_required(VERSION 3.31)
project(Trader)

set(CMAKE_CXX_STANDARD 20)

# Commande personnalisée pour générer la version à chaque build
add_custom_target(generate_version
        COMMAND ${CMAKE_SOURCE_DIR}/generate_version.sh
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Generating version information..."
        VERBATIM
)

# Inclure le sous-répertoire src
add_subdirectory(src)