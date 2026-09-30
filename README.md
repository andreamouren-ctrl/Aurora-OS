# Aurora OS

Aurora OS is a proprietary operating system project focused on speed, efficiency, privacy, resilience, and a deeply customizable desktop experience.

## Core direction

Aurora OS is **not** intended to be a Linux distribution or a visual clone of Windows, macOS, or existing desktop environments.

Its identity is based on:

- an efficiency-first kernel and system architecture;
- a modern memory subsystem designed around active, warm, cold, compressed, frozen, and disposable memory states;
- a highly customizable desktop as the user's primary environment;
- web access integrated directly into the desktop through isolated Web Surfaces;
- local-first operation with no mandatory online account;
- privacy by default and explicit capability-based permissions;
- aggressive suspension of inactive work to reduce CPU, RAM, I/O, and energy consumption;
- immutable/recoverable system components and transactional updates;
- native Activity Spaces for persistent work contexts;
- asynchronous system services designed to preserve UI responsiveness.

## Languages and toolchain

Aurora OS will **not** introduce a proprietary programming language.

Existing systems languages and toolchains will be selected pragmatically according to the needs of the kernel, drivers, services, and user-space components.

## Current phase

The project is currently in **Architecture / M0**.

Before expanding kernel implementation, the project's system model and architectural principles are being frozen in documentation.

See:

- [Vision](docs/VISION.md)
- [Architecture Principles](docs/ARCHITECTURE_PRINCIPLES.md)
- [Roadmap](docs/ROADMAP.md)

## Status

Early research and development. Interfaces and architecture are not yet stable.
