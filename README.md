Wiselib-TunSLIP6
================
This program is a SLIP (Serial Line IP - RFC1055) implementation for the IPv6 protocol stack of the Wiselib.
The tool has only one working mode at the moment which is a communication with nodes in the Wisebed testbed environment.
(Support for direct attached devices is planned)
The tool uses the modified java scripts of the Wisebed experimentation scripts for the communication. This program starts
the listen and send scripts as backgorund processes, and uses linux named pipes for the communication between the
this C and the java programs.

The code for the sensor devices is available in the Wisebed's GIT: https://github.com/ibr-alg/wiselib
For the border-router node, the IPv6_SLIP_RS must be defined in the wiselib.testing/algorithms/6lowpan/lowpan_config.h
The nodes can be reserved with the experimentation scripts
	https://github.com/wisebed/experimentation-scripts/wiki/Experimentation-Scripts-0.8

Then this tool can be started:

make
sudo ./wiselib_tunslip6 -W -R[RESERVATION-KEY] -B[BORDER-ROUTER-URN] [TUN-IP]