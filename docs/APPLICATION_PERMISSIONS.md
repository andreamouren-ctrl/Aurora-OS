# Aurora OS Application Permissions

Status: **Canonical draft**
Version: **0.1**

## Purpose

Aurora OS uses a runtime permission system inspired by the strongest parts of mobile permission models, adapted for a desktop operating system.

The core rule is:

> Installing an application does not grant it permission to perform sensitive operations.

Applications run with the minimum authority required to start. Additional authority is granted explicitly through capabilities.

## Permission flow

1. An application declares the capabilities it may use in its package manifest.
2. The application attempts a protected action.
3. The request reaches the **Aurora Permission Broker**.
4. Aurora OS evaluates existing grants and system policy.
5. If authorization is missing, the Desktop presents a system-controlled permission prompt.
6. The user's choice becomes a scoped capability grant.
7. The application receives only that capability, not general system authority.

Applications cannot draw their own trusted permission dialog or bypass the broker.

## Grant modes

Where appropriate, Aurora OS supports:

- **Allow once**
- **Allow while in use**
- **Always allow**
- **Allow selected scope**
- **Deny**
- **Deny and do not ask again**

The exact choices shown depend on the capability.

## Protected capabilities

Initial capability families include:

- microphone;
- camera;
- location;
- screen capture;
- screen/control accessibility APIs;
- clipboard read;
- clipboard write;
- selected files and folders;
- broad filesystem access;
- removable storage;
- USB and other hardware devices;
- Bluetooth devices;
- local network discovery;
- internet/network access;
- listening for inbound network connections;
- notifications;
- background execution;
- autostart/login launch;
- persistent services;
- inter-process communication;
- access to other applications' data;
- account/identity information;
- system settings modification;
- installation of system components;
- privileged driver interfaces.

## Scoped access

Aurora OS prefers the narrowest useful grant.

Examples:

- one file instead of the entire Documents directory;
- one directory instead of the whole filesystem;
- one USB device instead of all USB devices;
- one clipboard read instead of permanent clipboard monitoring;
- camera only while an app is visible and active;
- microphone only during a call;
- selected network destinations where policy requires it.

## Files and object access

Applications should normally obtain user files through a system picker or object broker.

The picker returns a capability to the selected resource. It does not reveal unrestricted filesystem access.

This allows a normal desktop workflow without giving every application access to the user's complete home directory.

## Background behavior is a permission

An application does not gain indefinite background execution merely because it has been launched once.

Background execution, autostart, persistent services, periodic work, and wake-from-suspension behavior are separately controlled.

This supports Aurora OS's efficiency-first design.

## Sensitive resource indicators

Aurora OS must visibly indicate active use of sensitive resources such as:

- microphone;
- camera;
- screen capture;
- location.

The indicator is rendered by the operating system, not by the application.

## Privacy & Permissions center

The Desktop provides a central system interface where the user can inspect:

- permissions granted to each application;
- applications holding a given capability;
- temporary grants;
- persistent grants;
- active sensitive-resource use;
- denied permissions;
- permission history where retained;
- background execution rights;
- autostart rights.

Any revocable permission can be removed from this interface.

## Manifest model

Application packages declare capabilities as required or optional.

Example conceptual manifest:

```text
capabilities:
  required:
    - network.outbound
  optional:
    - microphone
    - camera
    - files.user_selected
    - notifications
```

This declaration is metadata for installation review, policy, and runtime requests. It is not itself authorization.

## System and trusted components

Core Aurora OS components may require capabilities unavailable to ordinary applications.

Such authority must be assigned through explicit system policy and should still follow least-privilege principles.

Third-party software must not become trusted merely because it requests elevation.

## Design requirement

Permissions are part of the kernel/user-space security architecture, not only a Desktop feature.

The graphical permission prompt is a user interface for a lower-level capability mechanism enforced outside the requesting application.
