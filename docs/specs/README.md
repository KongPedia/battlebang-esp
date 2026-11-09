# Specification ownership

Current product/protocol/configuration contracts already live in the [firmware owner docs](../../firmware/README.md)
and relevant library/test owners; this directory does not replace them or claim the firmware is unimplemented.
Keep new maintained contracts beside the affected owner unless a genuinely cross-cutting specification needs a home.

For meaningful changes, the [firmware workflow](../../.agents/README.md#sdd-lite-tdd-and-selective-ddd) defines
specification-driven acceptance. Keep env/variant, consumers, safety, compatibility, evidence and rollback in the
issue/PR or an existing owner spec. Common's `spec-driven-development` procedure supplies an optional
[change-spec resource](https://github.com/KongPedia/agent-skills/blob/v0.1.1/plugins/common/skills/spec-driven-development/assets/change-spec.md)
when helpful; do not copy a template bundle into this consumer or introduce a spec framework.
[Decision rationale](../adr/README.md) remains distinct from behavioral acceptance.
