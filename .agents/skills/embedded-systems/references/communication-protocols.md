# Communication Protocols

## ESP32 compatibility and authority

Read the [ESP32 boundary and SDK references](../SKILL.md#esp32-compatibility-and-authority) first.
Use the selected Arduino/ESP-IDF version and existing product transport, not STM32 registers or ARM interrupt recipes.
The checks below describe design/review requirements, not tested drivers or permission to access hardware.
Do not change dependencies, flags, watchdogs or firmware configuration from this guide.

## I2C Master Implementation

- Confirm the board's pins, pull-ups, voltage, address convention and bus timing before changing the owner driver.
- Use the installed SDK's transaction API. Check status, transferred length and timeout; preserve repeated-start
  requirements for register reads instead of assuming separate write/read transactions are equivalent.
- Define bounded recovery for NACK, arbitration loss and a stuck bus. A timeout must release transaction resources;
  it does not establish that the peripheral saw no write.

## SPI Master Implementation

- Match clock polarity/phase, word size, bit order, chip-select lifetime and speed to the actual peripheral.
- Validate buffer lengths and lifetimes before submission. Keep DMA buffers owned until completion or confirmed abort;
  check the installed driver's alignment and memory-capability requirements.
- Bound each wait and handle completion, timeout and transfer errors separately. Do not reuse a decremented unsigned
  loop counter across waits, or treat an enable bit clearing as proof of successful DMA completion.
- Use the existing driver to release chip select and recover after failure; do not transplant register-level DMA setup.

## UART with Interrupt and Circular Buffer

- Reuse the existing product UART/USB transport and SDK driver. Keep ISR work bounded and use the matching ISR-safe APIs.
- Define full/empty behavior, capacity and overflow reporting before using a ring buffer. `volatile` is not cross-core
  synchronization; single-owner foreground queues are not automatically safe for ISR/task or multicore access.
- Preserve parser length/CRC rejection, resynchronization and bounded per-tick work. Keep binary application streams
  free of debug/provisioning text.
- Inspect the existing bounded [FrameTxQueue implementation](../../../../firmware/go2_nixo_framed_packet_uart/uart/runtime.cpp)
  and its [owner/API](../../../../firmware/go2_nixo_framed_packet_uart/uart/runtime.h) before adding another queue.
  It reports admission failure and supports partial consumption; it is not a generic synchronized ISR queue.

## CAN Bus Implementation

- Confirm the selected ESP chip, installed driver, external transceiver, termination, bitrate and identifier/frame mode.
  Do not assume an STM32 CAN mailbox layout or that every ESP target supports the same peripheral.
- Validate identifiers, frame flags and payload length before indexing/copying. Read only the caller-provided bytes;
  a short payload does not permit an unconditional eight-byte read. Handle remote frames according to the driver API.
- Check queue admission and actual transmit/error events separately. Bound retries and handle bus-off without replaying
  stale actuator commands; accepted/queued is not physical execution.

## Best Practices

- Keep one transaction/buffer owner and explicit timeout, overflow and recovery policies.
- Use the existing parser/transport and offline production-code seams before adding a new driver or parser.
- Preserve local safety work ahead of reconnect/network processing and protocol lease expiry under saturation.
- Builds, offline checks and ACKs do not prove electrical timing or physical success. Live serial/bus access needs the
  repository's separate target-specific authorization; reset/open can itself trigger outputs.
