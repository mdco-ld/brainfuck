#include <cstdio>
#include <iostream>
#include <memory>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <vector>

namespace runtime {

struct Instruction {
    enum class Type {
        Add,
        Sub,
        Right,
        Left,
        Loop,
        EndLoop,
    };
    Type type;
};

struct AddInsn : public Instruction {
    AddInsn() { type = Type::Add; }
};

struct SubInsn : public Instruction {
    SubInsn() { type = Type::Sub; }
};

struct RightInsn : public Instruction {
    RightInsn() { type = Type::Right; }
};

struct LeftInsn : public Instruction {
    LeftInsn() { type = Type::Left; }
};

struct LoopInsn : public Instruction {
    LoopInsn() { type = Type::Loop; }
};

struct EndLoopInsn : public Instruction {
    EndLoopInsn() { type = Type::EndLoop; }
};

struct VM {
    std::unique_ptr<char[50000]> memory;
    std::size_t pointer{0};
};

struct Block {
    std::vector<Instruction> instructions;
    template <typename T> void append(T &&insn) {
        instructions.push_back(insn);
    }
};

struct Program {
    std::vector<Block> blocks;
};

struct Emitter {};

struct JIT {};

}; // namespace runtime

void *allocate_function(std::size_t size) {
    void *fn_memory = mmap(0, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return fn_memory;
}

void write_function(void *fn_memory) {
    // FIXME: This is hardcoded for now
    unsigned char opcodes[] = {0x48, 0xB8, 0xF0, 0xDE, 0xBC, 0x9A,
                               0x78, 0x56, 0x34, 0x12, 0xC3};
    memcpy(fn_memory, opcodes, 11);
}

typedef unsigned long long (*FnPointer)();

FnPointer build_function() {
    void *fn = allocate_function(128);
    write_function(fn);
    return (FnPointer)fn;
}

int main() {
    FnPointer fn = build_function();
    unsigned long long x = fn();
    printf("%llx\n", x);
    return 0;
}