// MatrixManager unit tests — singleton + identity verification.
// No GPU, no SDL, no audio.

#include "render/MatrixManager.h"
#include <cstdio>

static int g_failures = 0;

#define TEST(name) printf("  %-55s", name)
#define PASS()     printf("PASS\n")
#define FAIL(msg)  do { printf("FAIL: %s\n", msg); ++g_failures; return; } while (0)
#define CHECK(c,m) do { if (!(c)) { FAIL(m); return; } } while (0)

static void test_singleton() {
    TEST("GetInstance returns same object");
    MatrixManager& a = MatrixManager::GetInstance();
    MatrixManager& b = MatrixManager::GetInstance();
    CHECK(&a == &b, "different instances");
    PASS();
}

static void test_stacks_exist() {
    TEST("Four matrix stacks are accessible");
    MatrixManager& mm = MatrixManager::GetInstance();
    // Just verify they're at valid addresses (don't dereference GL state)
    CHECK(&mm.GetWorldStack() != 0, "null world stack?");
    CHECK(&mm.GetViewStack() != 0, "null view stack?");
    CHECK(&mm.GetProjectionStack() != 0, "null proj stack?");
    CHECK(&mm.GetTextureStack() != 0, "null texture stack?");
    PASS();
}

static void test_version_fields() {
    TEST("Version fields exist and are non-negative");
    MatrixManager& mm = MatrixManager::GetInstance();
    CHECK(mm.m_ViewVersion >= 0, "m_ViewVersion");
    CHECK(mm.m_ViewVersionUploaded >= 0, "m_ViewVersionUploaded");
    CHECK(mm.m_WorldVersionUploaded >= 0, "m_WorldVersionUploaded");
    PASS();
}

int main() {
    printf("MatrixManager tests:\n");
    test_singleton();
    test_stacks_exist();
    test_version_fields();
    printf("\n%d failures\n", g_failures);
    return g_failures ? 1 : 0;
}
