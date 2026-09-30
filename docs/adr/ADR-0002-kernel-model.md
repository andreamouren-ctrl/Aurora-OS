# ADR-0002 — Modular Hybrid Capability Kernel

Status: Accepted
Date: 2026-09-30

## Decision

Aurora OS will use a modular hybrid kernel with capability-based authority.

The kernel retains latency-critical mechanisms such as scheduling, virtual memory, interrupt handling, IPC primitives, and capability validation.

Higher-level policy and complex services should run in isolated user-space components when doing so does not violate Aurora's latency and efficiency requirements.

## Reasons

- application permissions require an enforcement boundary below the Desktop;
- capability handles provide finer authority than administrator/non-administrator privilege;
- isolated services reduce the blast radius of failures;
- a pure microkernel is not required where extra IPC cost provides no practical benefit;
- a traditional unrestricted monolithic model conflicts with Aurora's privacy and least-privilege goals;
- the architecture remains benchmark-driven.

## Consequence

Aurora's package manifest and Permission Broker will eventually map human-facing permissions to kernel capabilities rather than directly opening global resources.
