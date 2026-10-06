#include <assert.h>
#include <stddef.h>
#include <stdio.h>

#include <aurora/identity/creation_access_policy.h>

int main(void) {
    assert(aurora_identity_pre_auth_create_allowed(0u));
    assert(!aurora_identity_pre_auth_create_allowed(1u));
    assert(!aurora_identity_pre_auth_create_allowed(2u));
    assert(!aurora_identity_pre_auth_create_allowed((size_t)-1));

    puts("Aurora Identity creation access policy tests passed.");
    return 0;
}
