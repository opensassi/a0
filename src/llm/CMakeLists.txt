add_library(llm_lib STATIC
    driven_provider.cpp
    deepseek_provider.cpp
    response_decoder.cpp
)
target_include_directories(llm_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(llm_lib PUBLIC
    shared_lib
    CURL::libcurl
    nlohmann_json::nlohmann_json
)
