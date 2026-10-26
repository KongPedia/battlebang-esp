# Specification ownership

Current product/protocol/configuration contracts already live in the [firmware owner docs](../../firmware/README.md)
and relevant library/test owners; this directory does not replace them or claim the firmware is unimplemented.
Keep new maintained contracts beside the affected owner unless a genuinely cross-cutting specification needs a home.

For meaningful changes, the [firmware workflow](../../.agents/README.md#sdd-lite-tdd-and-selective-ddd) defines
specification-driven acceptance. An issue/PR is sufficient for small changes; the retained optional
[firmware change form](../../.agents/templates/change-spec.md) captures env/variant, consumers, safety and rollback when
needed. Common's `spec-driven-development` procedure complements these project constraints; no mandatory blank form or
new spec framework is introduced. [Decision rationale](../adr/README.md) remains distinct from behavioral acceptance.
