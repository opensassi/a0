add_library(persistence_lib STATIC
    sqlite_store.cpp
    replay_engine.cpp
    build_identity.cpp
)

target_include_directories(persistence_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(persistence_lib PUBLIC shared_lib executor_lib ${CMAKE_DL_LIBS})
find_package(SQLite3 REQUIRED)
target_link_libraries(persistence_lib PUBLIC SQLite::SQLite3)
