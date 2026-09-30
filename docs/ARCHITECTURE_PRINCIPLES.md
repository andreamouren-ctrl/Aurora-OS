# Aurora OS Architecture Principles

Status: **Canonical draft**
Version: **0.1**

These principles constrain future implementation decisions.

## A. Performance is architectural

Performance regressions caused by unnecessary background services, avoidable copies, blocking APIs, excessive wakeups, or uncontrolled allocation growth are architectural issues.

## B. Memory pressure is managed progressively

Preferred pressure response:

1. reclaim disposable cache;
2. reduce speculative/warm cache;
3. freeze inactive workloads when appropriate;
4. compress cold anonymous memory;
5. page to storage when necessary;
6. terminate workloads only as a controlled last resort.

Exact policy will be benchmark-driven.

## C. Interactive work wins

Foreground input, rendering, audio, and active Activity Space workloads receive priority over maintenance tasks.

## D. Idle means idle

A system at rest should converge toward minimal CPU wakeups, minimal I/O, and minimal energy use.

## E. Desktop customization must not fragment the OS

Users can deeply customize presentation and interaction while system contracts, security, packaging, and service APIs remain stable.

## F. Web content is untrusted

Every Web Surface is sandboxed by default and receives only explicitly granted capabilities.

## G. Applications are untrusted by default

Applications cannot infer broad machine access merely because they are installed.

## H. Core system state is protected

Packages and applications must not freely mutate system directories or register arbitrary persistent components.

## I. Updates are transactional

An update either reaches a valid new state or the system can return to the prior valid state.

## J. Local functionality does not require the cloud

Authentication, boot, recovery, settings, storage, and ordinary applications remain available offline.

## K. APIs favor asynchronous operation

Slow I/O or services should not force UI or latency-sensitive threads to wait.

## L. Architecture isolation

Hardware-specific code is isolated behind architecture/HAL boundaries.

Initial target:
- x86_64

Planned targets:
- ARM64
- RISC-V64

## M. Existing languages, deliberate choices

No custom programming language will be created.

Language choice is component-specific and will be documented through Architecture Decision Records.

## N. Measure before optimizing policy

The project will build instrumentation for:

- boot time;
- resume time;
- scheduler latency;
- frame latency;
- memory pressure;
- compression efficiency;
- page-fault latency;
- I/O latency;
- service wakeups;
- CPU residency;
- power use where measurable.

Major optimization policy must be guided by measurements rather than intuition alone.

## O. Proprietary does not mean reinventing every primitive

Aurora OS may use established standards, specifications, boot protocols, algorithms, and carefully selected third-party components when doing so improves correctness or development speed.

The proprietary value lies in the integrated architecture and system behavior.
