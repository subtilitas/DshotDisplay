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
| [blackbox-logging.md](blackbox-logging.md) | Design |

## Shared context

Both features are on `dev/telemetry-logging`, cut from `main`. They are related:
KISS supplies higher-resolution voltage and current, and the log is the main
reason that resolution is worth having — a 0.25 V/LSB voltage trace is not worth
plotting.

KISS telemetry is partially implemented; SD logging is not implemented yet. Order of work is KISS first: the logger's field set
depends on what the telemetry layer can supply, and building the log around EDT
resolution first would mean revisiting the field table.
