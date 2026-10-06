# MRuby needs a native compiler for its bytecode generator and a separate
# target library. Keep their outputs in this build tree, so host and ARM
# builds cannot clean or overwrite each other's archives.
find_program(TM_RAKE rake)
if(NOT TM_RAKE)
    message(FATAL_ERROR "The extended runtime requires Ruby and rake on the build host")
endif()
set(mruby_dir "${THIRDPARTY_DIR}/mruby")
set(mruby_build "${CMAKE_BINARY_DIR}/mruby")
set(mruby_archive "${mruby_build}/target/lib/libmruby.a")
set(mruby_config "${mruby_build}/tic80.rb")
file(WRITE "${mruby_config}.in" "
MRuby::Build.new do |conf|
  toolchain :gcc
  conf.build_mrbc_exec
  conf.disable_libmruby
  conf.disable_presym
end
MRuby::CrossBuild.new('target') do |conf|
  toolchain :gcc
  conf.gembox '${TM_TIC80_SOURCE}/build/mruby/tic'
  conf.cc.command = ENV.fetch('TM_MRUBY_CC')
  conf.cc.flags = [ENV.fetch('TM_MRUBY_CFLAGS')]
  conf.linker.command = ENV.fetch('TM_MRUBY_CC')
  conf.linker.flags = [ENV.fetch('TM_MRUBY_LDFLAGS')]
  conf.archiver.command = ENV.fetch('TM_MRUBY_AR')
end
")
configure_file("${mruby_config}.in" "${mruby_config}" COPYONLY)
string(TOUPPER "${CMAKE_BUILD_TYPE}" build_type)
file(GLOB_RECURSE mruby_inputs CONFIGURE_DEPENDS
    "${mruby_dir}/src/*.c" "${mruby_dir}/include/*.h"
    "${mruby_dir}/mrbgems/*" "${mruby_dir}/lib/*.rb" "${mruby_dir}/tasks/*.rake")
add_custom_command(OUTPUT "${mruby_archive}"
    COMMAND "${CMAKE_COMMAND}" -E env
        "MRUBY_CONFIG=${mruby_config}" "MRUBY_BUILD_DIR=${mruby_build}"
        "TM_MRUBY_CC=${CMAKE_C_COMPILER}" "TM_MRUBY_AR=${CMAKE_AR}"
        "TM_MRUBY_CFLAGS=${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_${build_type}}"
        "TM_MRUBY_LDFLAGS=${CMAKE_EXE_LINKER_FLAGS}"
        "${TM_RAKE}" all
    DEPENDS "${mruby_config}" ${mruby_inputs}
    WORKING_DIRECTORY "${mruby_dir}" VERBATIM)
add_custom_target(tm_mruby_build DEPENDS "${mruby_archive}")
add_library(tm_mruby STATIC IMPORTED)
set_target_properties(tm_mruby PROPERTIES IMPORTED_LOCATION "${mruby_archive}")
add_dependencies(tm_mruby tm_mruby_build)
add_library(ruby STATIC "${TM_TIC80_SOURCE}/src/api/mruby.c" "${TM_TIC80_SOURCE}/src/api/parse_note.c")
target_compile_definitions(ruby INTERFACE TIC_BUILD_WITH_RUBY=1)
target_include_directories(ruby PRIVATE "${mruby_dir}/include" "${TM_TIC80_SOURCE}/include" "${TM_TIC80_SOURCE}/src")
target_link_libraries(ruby PRIVATE runtime tm_mruby)
