add_executable(opencdc
    entry.cpp
    main.cpp
    ir/graph.cpp
    frontend/slang_adapter.cpp
    clock/domain.cpp
    clock/resolve.cpp
    cdc/crossing.cpp
    cdc/synchronizer.cpp
    cdc/reconvergence.cpp
    cdc/waiver.cpp
    rules/rule.cpp
    report/report.cpp
)

target_include_directories(opencdc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(opencdc PRIVATE slang::slang)

install(TARGETS opencdc RUNTIME DESTINATION bin)
