# Identify each fitted chain head explicitly. Quartus 17 reporting did not
# retain the wildcard toggle rates consistently, so enumerate exact bits.
# FORCED fixes the head; FORCED IF ASYNCHRONOUS marks the remaining stages.
# Rates are transitions/second: 25 MHz exceeds the audio clock; 131 MHz
# exceeds the combined system/audio clocks on paths with mixed reset sources.
# Post-fit audits still require actual topology, clocks, timing and MTBF.
foreach spec {
    {read 0 8 25000000}
    {write 0 8 131000000}
    {played 1 31 25000000}
    {samples 0 32 25000000}
    {underruns 0 32 25000000}
} {
    lassign $spec stem start width rate
    for {set bit $start} {$bit < $width} {incr bit} {
        set first "emu|video|audio|${stem}_sync1\[$bit\]"
        set second "emu|video|audio|${stem}_sync2\[$bit\]"
        set_instance_assignment -name SYNCHRONIZER_IDENTIFICATION FORCED -to $first
        set_instance_assignment -name SYNCHRONIZER_IDENTIFICATION "FORCED IF ASYNCHRONOUS" -to $second
        set_instance_assignment -name SYNCHRONIZER_TOGGLE_RATE $rate -to $first
    }
}
set_instance_assignment -name SYNCHRONIZER_IDENTIFICATION FORCED -to {emu|video|audio|prefill_sync1}
set_instance_assignment -name SYNCHRONIZER_IDENTIFICATION "FORCED IF ASYNCHRONOUS" -to {emu|video|audio|prefill_sync2}
set_instance_assignment -name SYNCHRONIZER_TOGGLE_RATE 131000000 -to {emu|video|audio|prefill_sync1}
foreach spec {
    {emu|video vblank 3}
    {emu|video consumed 3}
    {emu|video|scanout toggle 3}
    {emu|video|scanout bank 2}
    {emu|video|scanout valid 2}
} {
    lassign $spec hierarchy stem stages
    set first "${hierarchy}|${stem}_sync\[0\]"
    set_instance_assignment -name SYNCHRONIZER_IDENTIFICATION FORCED -to $first
    set_instance_assignment -name SYNCHRONIZER_TOGGLE_RATE 131000000 -to $first
    for {set stage 1} {$stage < $stages} {incr stage} {
        set_instance_assignment -name SYNCHRONIZER_IDENTIFICATION "FORCED IF ASYNCHRONOUS" -to "${hierarchy}|${stem}_sync\[$stage\]"
    }
}
