add_library(fn_entities STATIC
    ActorManager.cpp
    Fruit.cpp
)
target_include_directories(fn_entities PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(fn_entities PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(fn_entities PUBLIC fn_engine fn_math fn_platform)
target_link_libraries(fruit-ninja PRIVATE fn_entities)
