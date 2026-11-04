# Changelog

## 1.0.0

- CAPL language for `.can` / `.cin` with TextMate grammar (event procedures incl. `sysvar_update`,
  `signal_update`, diagnostics, LIN/FlexRay/Ethernet/PDU events, `$signal` / `@sysvar::` access,
  `testcase` / `testfunction`, structs, enums, associative arrays).
- Completion, hover, signature help (with overloads), parameter-name inlay hints, outline,
  go-to-definition across `#include`s, include links, folding, semantic highlighting.
- Diagnostics: unbalanced brackets, duplicate sections/handlers/test cases, unknown event types,
  deprecated calls, optional unknown-function check — with quick fixes that add custom keywords.
- Configurable keywords, functions and hints via settings or watched JSON keyword files.
