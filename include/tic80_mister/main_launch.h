#ifndef TIC80_MISTER_MAIN_LAUNCH_H
#define TIC80_MISTER_MAIN_LAUNCH_H
/* Read-only launch context, evaluated after Main initializes a fresh FPGA.
 * 1: first MGL action replaces F0/F64; 0: raw launch or another first action;
 * -1: context unavailable/ambiguous. Never infer readiness from elapsed time.
 * proc_root is NULL on hardware; alternative roots are for memory fixtures. */
int tm_main_initial_cart(const char *proc_root);
/* Same pinned XML parser and first-valid-action rules as stock Main. */
int tm_main_mgl_initial_cart(const char *xml);
#endif
