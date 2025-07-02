# Collecter tous les fichiers sources dans le répertoire src
file(GLOB_RECURSE SOURCES "*.cpp" "*.c")
file(GLOB_RECURSE HEADERS "*.h" "*.hpp")

# Créer l'exécutable avec tous les fichiers sources
add_executable(Trader ${SOURCES} ${HEADERS})

# S'assurer que generate_version est exécuté avant la compilation
add_dependencies(Trader generate_version)

# Optionnel : définir le répertoire d'inclusion pour les headers
target_include_directories(Trader PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Optionnel : définir le répertoire de sortie de l'exécutable
set_target_properties(Trader PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)