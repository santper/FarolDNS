# Auto-incrementa o patch version baseado no número de commits git
execute_process(
    COMMAND git rev-list --count HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_COUNT
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

idf_component_register(SRCS "dns_server.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES "storage_manager" "network_manager" "esp_timer")
target_compile_definitions(${COMPONENT_LIB} PRIVATE "GIT_COMMIT_COUNT=${GIT_COMMIT_COUNT}")
