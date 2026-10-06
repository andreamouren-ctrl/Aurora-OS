#ifndef AURORA_IDENTITY_RUNTIME_STDIO_H
#define AURORA_IDENTITY_RUNTIME_STDIO_H

/*
 * The pinned Argon2 reference sources include <stdio.h> for optional diagnostic
 * and encoded-string helpers. The live Aurora Identity runtime links only the
 * raw Argon2id path and enables neither GENKAT nor stdio-backed diagnostics, so
 * no stdio surface is required here.
 */

#endif
