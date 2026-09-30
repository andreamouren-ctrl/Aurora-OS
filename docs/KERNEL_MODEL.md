# Aurora OS Kernel Model

Status: **Canonical draft**
Version: **0.1**

Aurora OS uses a **modular hybrid capability-kernel architecture**.

The goal is not to reproduce a classic monolithic kernel or a pure microkernel. Aurora keeps latency-critical mechanisms in privileged space while pushing policy and failure-prone functionality behind isolated interfaces where practical.

## Kernel responsibilities

The privileged kernel owns:

- CPU and interrupt control;
- physical and virtual memory;
- scheduling and per-CPU execution state;
- IPC primitives;
- capability enforcement;
- process and thread isolation;
- low-level timekeeping;
- minimal hardware-enablement code required to boot and maintain the machine;
- security boundaries that user-space components cannot bypass.

## User-space system services

Where latency and hardware constraints allow, higher-level policy belongs outside the kernel:

- package management;
- application lifecycle policy;
- permission UI and user consent;
- indexing;
- network policy;
- web services;
- desktop composition policy;
- update orchestration;
- AI services;
- non-critical device services.

A user-space service receives only the capabilities it needs.

## Driver direction

Aurora prefers isolated driver services when practical, especially for complex or high-risk device stacks.

Boot-critical and latency-sensitive hardware may use privileged kernel components where measurements justify it.

The driver boundary is a performance/security decision, not an ideology.

## Capability rule

Possessing an object name or identifier does not grant access.

A process must hold a valid kernel-issued capability with sufficient rights for the requested operation.

Capabilities are:

- unforgeable from user space;
- scoped;
- revocable;
- typed;
- rights-limited;
- transferable only when explicitly permitted;
- attenuable: delegation can remove rights but cannot invent new ones.

This capability layer is the enforcement foundation beneath Aurora's application permission system.

## Policy vs mechanism

Example:

1. an application asks for microphone access;
2. the Permission Broker decides whether user consent is required;
3. if allowed, the broker delegates a microphone capability;
4. the kernel validates that capability on every protected operation.

The Desktop prompt is policy and user experience.
The capability is the security mechanism.

## Performance rule

Moving functionality out of the kernel must not introduce avoidable copying or context switching.

Aurora IPC is therefore designed for:

- bounded messages;
- capability transfer;
- shared-memory data paths;
- zero-copy buffers where safe;
- asynchronous operation.

## Current bootstrap state

The current M1 scheduler is BSP-only. Application processors are brought online and parked until per-CPU scheduler state and locking rules are complete.
