# Run quartus_sta -t ../../tools/report_timing.tcl from build/fpga.
project_open TIC80
create_timing_netlist -model slow -temperature -40 -voltage 1100
read_sdc
update_timing_netlist
report_timing -setup -npaths 5 -detail full_path -file ../cold-critical.rpt
delete_timing_netlist
project_close
