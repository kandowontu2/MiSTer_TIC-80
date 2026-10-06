find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(pforth_dir "${THIRDPARTY_DIR}/pforth")
set(dictionary "${CMAKE_BINARY_DIR}/pforth/pfdicdat.h")
set(runner)
if(CMAKE_CROSSCOMPILING)
    if(NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "arm")
        message(FATAL_ERROR "Forth dictionary runner is configured for ARM cross builds only")
    endif()
    find_program(TM_QEMU_ARM qemu-arm)
    if(NOT TM_QEMU_ARM)
        message(FATAL_ERROR "ARM Forth dictionary generation requires qemu-arm (qemu-user)")
    endif()
    set(runner --runner "${TM_QEMU_ARM}")
endif()
file(STRINGS "${pforth_dir}/csrc/sources.cmake" files)
set(kernel)
foreach(file IN LISTS files)
    if(file MATCHES "\\.c$" AND NOT file STREQUAL "pfcustom.c")
        list(APPEND kernel "${pforth_dir}/csrc/${file}")
    endif()
endforeach()
file(GLOB_RECURSE dictionary_inputs CONFIGURE_DEPENDS "${pforth_dir}/fth/*.fth" "${pforth_dir}/csrc/*.h" "${pforth_dir}/csrc/*.c")
add_custom_command(OUTPUT "${dictionary}"
    COMMAND "${Python3_EXECUTABLE}" "${TM_INTEGRATION_SOURCE}/tools/build_forth_dictionary.py"
        --source "${pforth_dir}" --output "${dictionary}"
        --compiler "${CMAKE_C_COMPILER}"
        "--flags=${CMAKE_C_FLAGS} ${CMAKE_EXE_LINKER_FLAGS}"
        ${runner}
    DEPENDS ${dictionary_inputs} "${TM_INTEGRATION_SOURCE}/tools/build_forth_dictionary.py"
    VERBATIM)
add_custom_target(tm_forth_dictionary DEPENDS "${dictionary}")
add_library(forth STATIC ${kernel} "${TM_TIC80_SOURCE}/src/api/forth.c" "${TM_TIC80_SOURCE}/src/api/parse_note.c")
tm_stage_forth_stack_kernel(forth "${pforth_dir}/csrc/pf_inner.c")
add_dependencies(forth tm_forth_dictionary)
target_compile_definitions(forth INTERFACE TIC_BUILD_WITH_FORTH=1)
target_compile_definitions(forth PRIVATE PF_STATIC_DIC PF_SUPPORT_FP PF_NO_FILEIO PF_DEMAND_PAGING=0)
target_include_directories(forth PRIVATE "${CMAKE_BINARY_DIR}/pforth" "${pforth_dir}/csrc" "${TM_TIC80_SOURCE}/src" "${TM_TIC80_SOURCE}/include")
target_link_libraries(forth PRIVATE runtime)
