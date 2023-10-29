#include <cstdio>
#include <iostream>
#include <memory>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <vector>

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
    std::vector<std::unique_ptr<Instruction>> instructions;
    template <typename T> void append(T &insn) {
        instructions.push_back(std::make_unique(insn));
    }
};

struct Program {
    std::vector<Block> blocks;
};

namespace JIT {

enum class Register {
    EAX = 0b000,
};

struct Imm32 {
    static const size_t length = 4;
    unsigned int value{0};
    explicit Imm32(unsigned int val) : value(val) {
        bytes.resize(length);
        for (int i = 0; i < length; i++) {
            bytes[i] = val & 0xff;
            val >>= 8;
        }
    }

    const std::vector<char> &get_bytes() { return bytes; }

  private:
    std::vector<char> bytes;
};

struct Imm64 {
    static const size_t length = 8;
    unsigned int value{0};
    explicit Imm64(unsigned int val) : value(val) {
        bytes.resize(length);
        for (int i = 0; i < length; i++) {
            bytes[i] = val & 0xff;
            val >>= 8;
        }
    }

    const std::vector<char> &get_bytes() { return bytes; }

  private:
    std::vector<char> bytes;
};

struct Emitter {
    void ret() { buffer.emplace_back(0xC3); }
    void mov(Register reg, Imm32 value) {
        buffer.push_back(0xB8 | (int)reg);
        auto imm = value.get_bytes();
        buffer.insert(buffer.end(), imm.begin(), imm.end());
    }
    void movabs(Register reg, Imm64 value) {
        buffer.push_back(0x48);
        buffer.push_back(0xB8 | (int)reg);
        auto imm = value.get_bytes();
        buffer.insert(buffer.end(), imm.begin(), imm.end());
    }
    void add(Register reg, Imm32 value) {
        buffer.push_back(0x05);
        auto imm = value.get_bytes();
        buffer.insert(buffer.end(), imm.begin(), imm.end());
    }

    const std::vector<char> &get() { return buffer; }

  private:
    std::vector<char> buffer;
};

struct JIT {};
}; // namespace JIT

void *allocate_function(std::size_t size) {
    void *fn_memory = mmap(0, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return fn_memory;
}

void write_function(void *fn_memory) {
    JIT::Emitter emitter;
    emitter.mov(JIT::Register::EAX, JIT::Imm32(69));
    emitter.add(JIT::Register::EAX, JIT::Imm32(351));
    emitter.ret();
    std::vector<char> code = emitter.get();
    for (auto c : code) {
        printf("%x ", c & 0xff);
    }
    puts("");
    memcpy(fn_memory, code.data(), code.size());
}

typedef unsigned long long (*FnPointer)();

FnPointer build_function() {
    void *fn = allocate_function(128);
    write_function(fn);
    return (FnPointer)fn;
}

int main() {
    FnPointer fn = build_function();
    int x = fn();
    printf("%d\n", x);
    return 0;
}
