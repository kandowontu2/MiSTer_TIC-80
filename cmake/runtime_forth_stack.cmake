# Native TIC words pop their own operands. Encode their required depth in the
# unused byte of CALL_C so pForth can THROW before entering any C wrapper.
function(tm_patch_forth_stack variable)
    set(code "${${variable}}")
    string(REGEX MATCHALL "\\(CFunc0\\)tic_forth_[a-z0-9_]+" entries "${code}")
    string(REGEX MATCHALL "CreateGlueToC\\([^\n)]+\\)" registrations "${code}")
    list(LENGTH entries entry_count)
    list(LENGTH registrations registration_count)
    if(NOT entry_count EQUAL 62 OR NOT registration_count EQUAL entry_count)
        message(FATAL_ERROR "Pinned Forth native word registry changed")
    endif()
    math(EXPR last "${entry_count} - 1")
    foreach(index RANGE 0 ${last})
        list(GET entries ${index} entry)
        string(REPLACE "(CFunc0)" "" name "${entry}")
        string(FIND "${code}" "static cell_t ${name}(void)" start)
        if(start EQUAL -1)
            message(FATAL_ERROR "Pinned Forth native function missing: ${name}")
        endif()
        string(SUBSTRING "${code}" ${start} -1 tail)
        string(FIND "${tail}" "\nstatic " end)
        if(end EQUAL -1)
            message(FATAL_ERROR "Pinned Forth native function boundary missing: ${name}")
        endif()
        string(SUBSTRING "${tail}" 0 ${end} body)
        string(REGEX MATCHALL "pfPopFromStack\\(\\)" pops "${body}")
        list(LENGTH pops required)
        if(required GREATER 254)
            message(FATAL_ERROR "Forth required depth does not fit CALL_C metadata")
        endif()
        list(GET registrations ${index} previous)
        if(NOT previous MATCHES ", *0\\)$")
            message(FATAL_ERROR "Pinned Forth zero-parameter C ABI changed")
        endif()
        string(REGEX REPLACE ", *0\\)$" ", ${required})" updated "${previous}")
        string(REPLACE "CreateGlueToC(" "tm_forth_glue(" updated "${updated}")
        tm_fft_replace(code "${previous}" "${updated}")
    endforeach()
    set(helper [=[static Err tm_forth_glue(const char* name, ucell_t index,
                         cell_t return_mode, ucell_t required)
{
    char forth_name[PF_NAME_SIZE_SAFE];
    CStringToForth(forth_name, name, sizeof forth_name);
    /* NumParams stays zero: the registered CFunc0 pops its own operands. */
    /* A nonzero marker distinguishes TIC words from bootstrap C-test glue. */
    ucell_t packed = (index & 0xffff) | ((required + 1) << 16)
        | ((ucell_t)return_mode << 31);
    ffCreateSecondaryHeader(forth_name);
    CODE_COMMA(ID_CALL_C);
    CODE_COMMA(packed);
    ffFinishSecondary();
    return 0;
}

]=])
    tm_fft_replace(code "Err CompileCustomFunctions(void)" "${helper}Err CompileCustomFunctions(void)")
    set(${variable} "${code}" PARENT_SCOPE)
endfunction()

function(tm_stage_forth_stack_kernel target original)
    file(SHA256 "${original}" original_sha)
    if(NOT original_sha STREQUAL "a14400cf78a2ea4cb372e0b70d0e8a457c8cf5986159af73fa56f1a531e7bc75")
        message(FATAL_ERROR "Pinned pForth CALL_C source changed")
    endif()
    file(READ "${original}" code)
    set(previous [=[        case ID_CALL_C:
            SAVE_REGISTERS;
            Scratch = READ_CELL_DIC(InsPtr);
            InsPtr += PF_CELL_SIZE;
            CallUserFunction( Scratch & 0xFFFF,
                (Scratch >> 31) & 1,
                (Scratch >> 24) & 0x7F );
            LOAD_REGISTERS;
            endcase;]=])
    set(updated [=[        case ID_CALL_C: {
            SAVE_REGISTERS;
            Scratch = READ_CELL_DIC(InsPtr);
            InsPtr += PF_CELL_SIZE;
            cell_t encoded_depth = (Scratch >> 16) & 0xff;
            cell_t parameters = (Scratch >> 24) & 0x7f;
            /* Bootstrap C-test words have different C prototypes and refer to
             * indices reused by TIC's table. Never dispatch those aliases. */
            if (!encoded_depth || parameters ||
                    (Scratch & 0xffff) >= TM_FORTH_NATIVE_COUNT) {
                LOAD_REGISTERS;
                M_THROW(THROW_UNDEFINED_WORD);
                endcase;
            }
            cell_t required = encoded_depth - 1;
            if (DATA_STACK_DEPTH < required) {
                LOAD_REGISTERS;
                M_THROW(THROW_STACK_UNDERFLOW);
                endcase;
            }
            CallUserFunction( Scratch & 0xFFFF,
                (Scratch >> 31) & 1,
                parameters );
            LOAD_REGISTERS;
            endcase;
        }]=])
    tm_fft_replace(code "${previous}" "${updated}")
    set(generated "${CMAKE_BINARY_DIR}/runtime_adapters/pf_inner.c")
    tm_write_generated("${generated}" "${code}")
    get_target_property(sources ${target} SOURCES)
    list(REMOVE_ITEM sources "${original}")
    set_property(TARGET ${target} PROPERTY SOURCES "${sources}")
    target_sources(${target} PRIVATE "${generated}")
    target_compile_definitions(${target} PRIVATE TM_FORTH_NATIVE_COUNT=62)
endfunction()
