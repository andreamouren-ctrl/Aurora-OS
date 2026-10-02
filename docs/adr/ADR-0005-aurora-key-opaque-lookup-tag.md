# ADR-0005 — Aurora Key opaque lookup tag

Status: **Accepted**
Date: **2026-10-02**

## Context

Aurora Key is both the user's secret login credential and the only value entered on the default cold-login surface. Aurora intentionally does not require a separate visible username.

A persistent Identity Service must therefore locate the candidate Aurora Key credential record before performing the expensive memory-hard verifier check.

Storing either the raw Aurora Key or a plain deterministic fast hash of it would violate Aurora Identity's security goals. A plain hash would also create an efficient offline guessing oracle if the identity database were copied.

## Decision

The Identity Service architecture includes an **opaque keyed lookup tag** for Aurora Key credential resolution.

The tag is derived from the normalized Aurora Key using a protected secret available only to the trusted identity/crypto boundary. The production construction must use a reviewed keyed pseudorandom-function/MAC-style primitive or an equivalent construction selected with Aurora's crypto layer.

The protected lookup key/pepper is not stored as ordinary identity-database data.

Authentication becomes conceptually:

```text
candidate Aurora Key
 -> normalize
 -> derive opaque keyed lookup tag
 -> resolve credential record
 -> enforce persisted throttle state
 -> run stored Argon2id verifier
 -> issue authenticated result/session grant
```

The lookup tag is an index/locator only. It is not sufficient authentication proof and never replaces Argon2id verification.

## Requirements

- never persist the raw Aurora Key;
- never use an unkeyed fast hash of the Aurora Key as the production lookup index;
- treat the lookup secret as protected system secret material;
- version the lookup-tag derivation so migration is possible;
- always perform the normal credential verifier after record resolution;
- do not expose lookup tags through normal UI, logs, audit output, or application APIs;
- database backup policy must preserve separation between verifier data and protected lookup-secret material where the final key-storage design permits it;
- unknown-key abuse must additionally be bounded by machine/global throttling because an unknown key has no per-record throttle state.

## Consequences

The first isolated Aurora Identity Core exposes lookup-tag derivation as a crypto-provider interface but deliberately does not implement the production primitive yet.

Host tests may use deterministic fake tags solely to exercise control flow. Such test providers must never be linked into the production Aurora OS authentication path.

This decision keeps the current no-username UX while avoiding a persistent plaintext credential or an obvious fast-hash lookup oracle.
