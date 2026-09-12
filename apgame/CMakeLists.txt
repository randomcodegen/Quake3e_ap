cmake_minimum_required(VERSION 3.20)
project(q3ap_modules C)

include(CTest)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "q3ap currently builds 64-bit Windows native modules only")
endif()

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

file(GLOB GAME_SOURCES CONFIGURE_DEPENDS code/game/*.c)
list(REMOVE_ITEM GAME_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/code/game/bg_lib.c")
list(REMOVE_ITEM GAME_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/code/game/g_rankings.c")

file(GLOB UI_SOURCES CONFIGURE_DEPENDS code/ui/*.c)
list(REMOVE_ITEM UI_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/code/ui/ui_login.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/code/ui/ui_rankings.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/code/ui/ui_rankstatus.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/code/ui/ui_signup.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/code/ui/ui_specifyleague.c"
)

add_library(qagame MODULE ${GAME_SOURCES} code/game/game.def)
target_compile_definitions(qagame PRIVATE QAGAME)
set_target_properties(qagame PROPERTIES
    PREFIX ""
    OUTPUT_NAME "qagamex86_64"
)

add_library(ui MODULE
    ${UI_SOURCES}
    code/game/bg_misc.c
    code/game/q_math.c
    code/game/q_shared.c
    code/ui/ui.def
)
set_target_properties(ui PROPERTIES
    PREFIX ""
    OUTPUT_NAME "uix86_64"
)

foreach(target qagame ui)
    target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(${target} PRIVATE /W3)
    target_link_options(${target} PRIVATE /Brepro)
    install(TARGETS ${target} RUNTIME DESTINATION q3ap LIBRARY DESTINATION q3ap)
endforeach()

if(BUILD_TESTING)
    add_executable(q3ap_game_state_test tests/ap_game_state_test.c code/game/g_ap.c
        ../code/qcommon/ap_runtime_state.c)
    target_compile_definitions(q3ap_game_state_test PRIVATE _CRT_SECURE_NO_WARNINGS)
    add_test(NAME q3ap_game_state COMMAND q3ap_game_state_test)
endif()
