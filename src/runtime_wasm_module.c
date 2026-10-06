/* Export names are owned strings allocated by the pinned WASM parser, but
 * m3_FreeModule omits these module fields. Release them with the module;
 * function and global names remain owned by their upstream cleanup paths. */
#include "m3_env.h"
#define m3_FreeModule tm_upstream_free_module
#include TM_UPSTREAM_WASM_MODULE
#undef m3_FreeModule

void m3_FreeModule(IM3Module module)
{
    if (module) {
        m3_Free(module->memoryExportName);
        m3_Free(module->table0ExportName);
        FreeImportInfo(&module->memoryImport);
    }
    tm_upstream_free_module(module);
}
