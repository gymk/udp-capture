# udp-cature

Utility to capture UDP multicast content to a file

Compile: g++ udp_capture.c -lpthread

Run: ./a.out <multcast_address> <port> <size in byts to capture>

<TO DO>
Currently it writes each captured buffer into file. This will affect HDD seriously.
Need to add one more layer of buffer to avoid frequent write to HDD.
