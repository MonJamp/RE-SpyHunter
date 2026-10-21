# Mission level loading requirements

First implementation slice, selected 2026-10-20 from the target's static Mission 1 loading trace. The evidence applies to USA `SLUS_200.56` (SHA-256 `EFC55119F17E35F6AEE95335499F5845D5FAD4274434E803449C6E14CBFA73AB`); runtime order and fidelity have not been confirmed in PCSX2.

Source record: [Mission 1 loading investigation](../../../docs/level-one-loading.md). This native work covers package discovery and bounded read validation. It does not claim to deserialize proprietary scene formats, create a rendered level, or implement gameplay.

## ML-01 — Index every serialized PCK resource

Keep the internal resource name used by existing consumers and also retain the length-prefixed load path immediately preceding each serialized record. When the serialized header has no extension-bearing name, derive the display/member basename from that load path. Preserve offset and inferred extent behavior and keep the archive read-only.

**Acceptance:** `PckEntry` exposes both names; records without an extension-bearing internal name remain indexable; existing archive consumers can continue looking up their current names. The Mission 1 census contains 287 static and 117 dynamic candidate records, subject to the static research caveat that this is header-based coverage, not type-specific parsing.

**Status:** implemented in `GameRuntime/PckArchive`; the updated index has not yet been run against PG1 archives.

## ML-02 — Resolve and check the Mission 1 package

For the evidence-backed first mission only, locate `PG1/PG1.PCK` and `PG1/PG1D.PCK`, verify the required track, water group, environment and eight external-model load paths, and require `PG1.AI`, `PG1.WTR`, `OBJECTS.TXT` and `PFX.TXT` in the mission directory. Fail with the missing path/resource in the error.

**Acceptance:** package inspection succeeds for the extracted USA data set and rejects a missing archive, required archive resource, or loose companion with a specific diagnostic. Do not infer support for other missions from this Mission 1 manifest.

**Status:** implemented as `inspectProvingGroundsPackage`; not yet run against the extracted package or connected to Mission Data START.

## ML-03 — Read package data in recovered order with bounded progress

Inspect static archive records first, then dynamic archive records, followed by loose mission companions. Read in chunks no larger than 64 KiB and report stage, current item, completed bytes and item total through the caller callback. Stop on the first short read or I/O error.

**Acceptance:** the callback reports monotonically increasing byte counts per item/stage; no operation buffers an entire PCK or companion file; the stage sequence is static archive → dynamic archive → AI, water, object definitions, effect counts. Progress is a host byte-read indicator and must not be described as the original game's item-based 405-count bar.

**Status:** implemented as bounded package inspection; callback order and I/O have not yet been exercised against the extracted package. Original archive deserialization and effect/object initialization remain future work.
