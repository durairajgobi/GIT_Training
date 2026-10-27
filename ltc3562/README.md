# LTC3562 I2C regulator driver

Portable C99 driver for the LTC3562 quad step-down regulator (2 × 600 mA,
2 × 400 mA) that sets output voltages over I2C.

| Rail  | Type | Voltage set over I2C                        |
|-------|------|---------------------------------------------|
| R600A | A    | FB voltage 425–800 mV (output = FB × (1 + Rtop/Rbot)) |
| R400A | A    | FB voltage 425–800 mV (output = FB × (1 + Rtop/Rbot)) |
| R600B | B    | Output voltage 600–3775 mV                   |
| R400B | B    | Output voltage 600–3775 mV                   |

All steps are 25 mV. Each rail can run in pulse-skip, LDO, Burst or forced
Burst mode.

The LTC3562 can't be read over I2C, so the driver stores a copy of each
rail's enable, mode and DAC code and sends a complete two-byte command every
time it writes. That copy is only updated after a write succeeds.

## Layout

```
include/ltc3562.h          public API + register definitions
src/ltc3562.c              driver (no platform dependencies)
examples/ltc3562_linux.c   CLI using Linux /dev/i2c-N
tests/test_ltc3562.c       host unit tests against a mock I2C bus
```

## Porting

Write one callback for your I2C peripheral:

```c
int my_i2c_write(void *user, uint8_t addr7, const uint8_t *buf, size_t len);

ltc3562_dev_t pmic;
ltc3562_init(&pmic, my_i2c_write, &my_bus, 0);           /* all rails off */

ltc3562_configure(&pmic, LTC3562_R600B, 1800,
                  LTC3562_MODE_BURST, true, NULL);        /* 1.8 V, on    */

ltc3562_set_divider(&pmic, LTC3562_R600A, 309000, 100000);
ltc3562_configure(&pmic, LTC3562_R600A, 3300,
                  LTC3562_MODE_PULSE_SKIP, true, NULL);   /* ~3.3 V, on   */
```

A requested voltage is rounded to the nearest step the rail can produce, and
the `actual_mv` out-parameter returns the voltage that was programmed.
Requests more than half a step outside the rail's range return
`LTC3562_ERR_RANGE`.

The RUN pins for R600A/R400A are not handled by the driver. Drive them from
your board code.

## Build & test

```
make test                         # host unit tests
make all                          # also builds build/ltc3562_linux
./build/ltc3562_linux 1 R600B 1800 burst
```
