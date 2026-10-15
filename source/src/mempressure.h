/* Auxiliary memory-pressure mode, linked into the same binary as the PoC so a
 * reproduction needs exactly one executable on the device.
 *
 * Usage: <binary> --mempressure [MiB]
 */
#ifndef MEMPRESSURE_H
#define MEMPRESSURE_H

int mempressure_main(int argc, char **argv);

#endif /* MEMPRESSURE_H */
