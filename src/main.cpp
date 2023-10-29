#include <cstdio>
#include <string.h>
#include <sys/mman.h>

void *allocate_function(std::size_t size) {
    void *fn_memory = mmap(0, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return fn_memory;
}

int main() {
    void *fn_memory = allocate_function(128);
    unsigned char opcodes[] = {0x48, 0xB8, 0xF0, 0xDE, 0xBC, 0x9A,
                               0x78, 0x56, 0x34, 0x12, 0xC3};
    memcpy(fn_memory, opcodes, 11);
    void *fn_pointer = fn_memory;
    unsigned long long (*fn)() = (unsigned long long (*)())fn_pointer;
    unsigned long long x = fn();
    printf("%llx\n", x);
    return 0;
}
