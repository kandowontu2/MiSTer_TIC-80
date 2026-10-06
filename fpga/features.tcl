# HDMI/RGB development profile. The optional YC encoder has an unresolved
# setup path; enable only for analysis, not for the qualified development RBF.
if {[info exists ::env(TM_ENABLE_YC)] && $::env(TM_ENABLE_YC) == "1"} {
    puts "TIC-80: experimental YC encoder enabled"
} else {
    set_global_assignment -name VERILOG_MACRO "MISTER_DISABLE_YC=1"
}
