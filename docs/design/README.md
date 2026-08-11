# Design notes

Documents here describe work that is planned or in progress. They are written
before the code so the expensive decisions — protocol details, pin choices, what
runs on which core — can be argued about while they are still cheap to change.

A document that no longer matches the code is a bug. When a feature lands,
either fold the document into the README and delete it, or update it to describe
what was actually built and why it diverged.

| Document | Status |
|---|---|
| [kiss-telemetry.md](kiss-telemetry.md) | In progress |
| [blackbox-logging.md](blackbox-logging.md) | In progress |

## Shared context

Both features landed on `dev/telemetry-logging`, cut from `main`. They are related:
KISS supplies higher-resolution voltage and current, and the log is the main
reason that resolution is worth having — a 0.25 V/LSB voltage trace is not worth
plotting.

Both are partially implemented. KISS telemetry decodes and merges but has no UI;
SD logging has a validated encoder, a ring buffer and a FatFs writer, but no UI
and no hardware bring-up. Neither has run against a real ESC or a real card.
